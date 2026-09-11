import asyncio
import json
from contextlib import asynccontextmanager
from enum import Enum
from typing import List, Dict, Any, AsyncIterator
from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Depends
from pydantic import BaseModel
import rabbitmq
from sqlalchemy.ext.asyncio import AsyncSession, create_async_engine, async_sessionmaker
from sqlalchemy.orm import declarative_base, Mapped, mapped_column
from sqlalchemy import String, Integer, Boolean, select

# Asynchronous SQLAlchemy ORM setup for a local SQLite database.
DATABASE_URL = "sqlite+aiosqlite:///./orders.db"
engine = create_async_engine(DATABASE_URL, echo=False)
AsyncSessionLocal = async_sessionmaker(engine, expire_on_commit=False)
Base = declarative_base()


class OrderModel(Base):
    """Python ORM model mirroring the C++ matching engine structure."""
    __tablename__ = "orders"

    id: Mapped[int] = mapped_column(primary_key=True, autoincrement=True)
    order_id: Mapped[int] = mapped_column(unique=True, index=True)
    symbol: Mapped[str] = mapped_column(String)
    side: Mapped[str] = mapped_column(String)
    order_type: Mapped[str] = mapped_column(String)
    price: Mapped[int] = mapped_column(Integer)
    quantity: Mapped[int] = mapped_column(Integer)
    status: Mapped[str] = mapped_column(String, default="PENDING")


class OutboxEvent(Base):
    """SQLAlchemy outbox model tracking unprocessed events for reliable dispatch."""
    __tablename__ = "outbox_events"

    id: Mapped[int] = mapped_column(primary_key=True, autoincrement=True)
    payload: Mapped[str] = mapped_column(String)
    processed: Mapped[bool] = mapped_column(Boolean, default=False)


async def get_db() -> AsyncIterator[AsyncSession]:
    async with AsyncSessionLocal() as session:
        yield session

# Pydantic models for order requests.

class Side(str, Enum):
    BUY = "BUY"
    SELL = "SELL"


class OrderType(str, Enum):
    LIMIT = "LIMIT"
    MARKET = "MARKET"


class OrderRequest(BaseModel):
    order_id: int
    symbol: str
    side: Side
    type: OrderType
    price: int = 0
    quantity: int
    timestamp: int


class CancelRequest(BaseModel):
    order_id: int
    symbol: str


class ModifyRequest(BaseModel):
    order_id: int
    symbol: str
    new_price: int
    new_quantity: int


class ConnectionManager:
    """WebSocket manager handling active connections and broadcasting messages."""
    def __init__(self) -> None:
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket) -> None:
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: Dict[str, Any]) -> None:
        for connection in list(self.active_connections):
            try:
                await connection.send_json(message)
            except Exception:
                self.disconnect(connection)


manager: ConnectionManager = ConnectionManager()


async def consume_rabbitmq() -> None:
    """Async RabbitMQ consumer parsing and broadcasting trades to WebSockets."""
    try:
        async with rabbitmq.trades_queue.iterator() as queue_iter:
            async for message in queue_iter:
                async with message.process():
                    trade_json_string: str = message.body.decode("utf-8")
                    trade_data: Dict[str, Any] = json.loads(trade_json_string)
                    trade_data["message_type"] = "TRADE"
                    await manager.broadcast(trade_data)
    except asyncio.CancelledError:
        print("Stopped listening on RabbitMQ (trades).")
    except Exception as e:
        print(f"RabbitMQ consumer error (trades): {e}")


async def consume_execution_reports() -> None:
    """Async RabbitMQ consumer updating order statuses and broadcasting reports."""
    try:
        async with rabbitmq.execution_reports_queue.iterator() as queue_iter:
            async for message in queue_iter:
                async with message.process():
                    report_json_string: str = message.body.decode("utf-8")
                    report_data: Dict[str, Any] = json.loads(report_json_string)

                    order_id = report_data.get("order_id")
                    new_status = report_data.get("status")

                    if order_id is not None and new_status is not None:
                        async with AsyncSessionLocal() as db:
                            result = await db.execute(
                                select(OrderModel).where(OrderModel.order_id == order_id)
                            )
                            db_order = result.scalars().first()

                            if db_order:
                                db_order.status = new_status
                                await db.commit()
                                print(f"Updated order {order_id} status to {new_status}")

                        report_data["message_type"] = "EXECUTION_REPORT"
                        await manager.broadcast(report_data)

    except asyncio.CancelledError:
        print("Stopped listening to RabbitMQ reports.")
    except Exception as e:
        print(f"RabbitMQ report consumer error: {e}")

async def process_outbox() -> None:
    """Background worker publishing unprocessed outbox events to RabbitMQ."""
    while True:
        try:
            async with AsyncSessionLocal() as db:
                result = await db.execute(
                    select(OutboxEvent).where(OutboxEvent.processed == False).order_by(OutboxEvent.id)
                )
                events = result.scalars().all()

                for event in events:
                    payload_dict = json.loads(event.payload)
                    await rabbitmq.publish_order(payload_dict)

                    event.processed = True
                    db.add(event)

                await db.commit()
        except Exception as e:
            print(f"Outbox Worker error: {e}")

        await asyncio.sleep(1)


@asynccontextmanager
async def lifespan(app: FastAPI) -> AsyncIterator[None]:
    """FastAPI lifespan managing database, RabbitMQ, and background worker tasks."""
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)

    print("Starting server... Connecting to RabbitMQ.")

    try:
        await rabbitmq.init_rabbitmq()
        consumer_task = asyncio.create_task(consume_rabbitmq())
        reports_task = asyncio.create_task(consume_execution_reports())
    except Exception as e:
        print(f"Failed to connect to RabbitMQ on startup: {e}")
        print("Application running in fallback mode (Outbox collecting orders).")
        consumer_task = None
        reports_task = None

    outbox_task = asyncio.create_task(process_outbox())

    yield

    print("Shutting down server")
    if consumer_task:
        consumer_task.cancel()
    if reports_task:
        reports_task.cancel()
    outbox_task.cancel()

    if hasattr(rabbitmq, 'rabbitmq_connection') and rabbitmq.rabbitmq_connection:
        await rabbitmq.rabbitmq_connection.close()


app: FastAPI = FastAPI(lifespan=lifespan)

# Order management endpoints
@app.post("/orders", status_code=202)
async def create_order(order: OrderRequest, db: AsyncSession = Depends(get_db)) -> Dict[str, str]:
    async with db.begin():
        db_order = OrderModel(
            order_id=order.order_id,
            symbol=order.symbol,
            side=order.side,
            order_type=order.type,
            price=order.price,
            quantity=order.quantity,
            status="PENDING"
        )
        db.add(db_order)

        order_dict: Dict[str, Any] = order.model_dump()
        order_dict["action"] = "ADD"
        outbox_event = OutboxEvent(payload=json.dumps(order_dict))
        db.add(outbox_event)

    return {"message": "Order accepted for processing."}


@app.delete("/orders", status_code=202)
async def cancel_order(req: CancelRequest, db: AsyncSession = Depends(get_db)) -> Dict[str, str]:
    async with db.begin():
        payload: Dict[str, Any] = req.model_dump()
        payload["action"] = "CANCEL"
        outbox_event = OutboxEvent(payload=json.dumps(payload))
        db.add(outbox_event)

    return {"message": "Cancellation request accepted and saved."}


@app.put("/orders", status_code=202)
async def modify_order(req: ModifyRequest, db: AsyncSession = Depends(get_db)) -> Dict[str, str]:
    async with db.begin():
        payload: Dict[str, Any] = req.model_dump()
        payload["action"] = "MODIFY"
        outbox_event = OutboxEvent(payload=json.dumps(payload))
        db.add(outbox_event)

    return {"message": "Modification request accepted and saved."}


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        print("User disconnected from WebSocket")
    finally:
        manager.disconnect(websocket)
