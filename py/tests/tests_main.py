import json
import pytest
from typing import AsyncIterator, Dict, Any
from unittest.mock import AsyncMock
from httpx import AsyncClient, ASGITransport
from sqlalchemy.ext.asyncio import create_async_engine, async_sessionmaker, AsyncSession
from fastapi import WebSocket
import pytest_asyncio
from src.main import (app, get_db, Base, ConnectionManager, OrderModel, OutboxEvent, Side, OrderType)

# Asynchronous SQLAlchemy ORM setup for an in-memory SQLite test database.
TEST_DATABASE_URL: str = "sqlite+aiosqlite:///:memory:"
engine = create_async_engine(TEST_DATABASE_URL, echo=False)
TestingSessionLocal = async_sessionmaker(engine, expire_on_commit=False)

# FastAPI dependency override injecting an isolated test database session.
async def override_get_db() -> AsyncIterator[AsyncSession]:
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    async with TestingSessionLocal() as session:
        yield session

app.dependency_overrides[get_db] = override_get_db

@pytest_asyncio.fixture(scope="function")
async def client() -> AsyncIterator[AsyncClient]:
    """Pytest fixture providing an async HTTP client for API testing."""
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as ac:
        yield ac

@pytest.mark.asyncio
async def test_create_order(client: AsyncClient) -> None:
    """Test verifying order creation, database persistence, and outbox event."""
    payload: Dict[str, Any] = {
        "order_id": 101,
        "symbol": "BTCUSD",
        "side": Side.BUY.value,
        "type": OrderType.LIMIT.value,
        "price": 50000,
        "quantity": 1,
        "timestamp": 1690000000
    }

    response = await client.post("/orders", json=payload)

    assert response.status_code == 202
    assert response.json() == {"message": "Order accepted for processing."}

    async with TestingSessionLocal() as session:
        order = await session.get(OrderModel, 1)
        assert order is not None
        assert order.order_id == 101
        assert order.status == "PENDING"

        outbox = await session.get(OutboxEvent, 1)
        assert outbox is not None
        outbox_payload = json.loads(outbox.payload)
        assert outbox_payload["action"] == "ADD"
        assert outbox_payload["order_id"] == 101


@pytest.mark.asyncio
async def test_cancel_order(client: AsyncClient) -> None:
    """Test verifying order cancellation, database persistence, and outbox event."""
    payload: Dict[str, Any] = {
        "order_id": 101,
        "symbol": "BTCUSD"
    }

    response = await client.request("DELETE", "/orders", json=payload)

    assert response.status_code == 202
    assert response.json() == {"message": "Cancellation request accepted and saved."}

    async with TestingSessionLocal() as session:
        outbox = await session.get(OutboxEvent, 1)
        assert outbox is not None
        outbox_payload = json.loads(outbox.payload)
        assert outbox_payload["action"] == "CANCEL"


@pytest.mark.asyncio
async def test_modify_order(client: AsyncClient) -> None:
    """Test verifying order modification endpoint and outbox event persistence."""
    payload: Dict[str, Any] = {
        "order_id": 101,
        "symbol": "BTCUSD",
        "new_price": 51000,
        "new_quantity": 2
    }

    response = await client.put("/orders", json=payload)

    assert response.status_code == 202
    assert response.json() == {"message": "Modification request accepted and saved."}

    async with TestingSessionLocal() as session:
        outbox = await session.get(OutboxEvent, 1)
        assert outbox is not None
        outbox_payload = json.loads(outbox.payload)
        assert outbox_payload["action"] == "MODIFY"


@pytest.mark.asyncio
async def test_connection_manager() -> None:
    """Test verifying ConnectionManager WebSocket connection, broadcasting, and disconnection."""
    manager: ConnectionManager = ConnectionManager()

    mock_ws = AsyncMock(spec=WebSocket)

    await manager.connect(mock_ws)
    mock_ws.accept.assert_awaited_once()
    assert len(manager.active_connections) == 1
    assert manager.active_connections[0] == mock_ws

    test_message: Dict[str, Any] = {"event": "TRADE", "price": 50000}
    await manager.broadcast(test_message)
    mock_ws.send_json.assert_awaited_once_with(test_message)

    manager.disconnect(mock_ws)
    assert len(manager.active_connections) == 0


@pytest.mark.asyncio
async def test_connection_manager_broadcast_failure() -> None:
    """Test verifying ConnectionManager removes failed WebSockets during broadcast."""
    manager: ConnectionManager = ConnectionManager()
    mock_ws = AsyncMock(spec=WebSocket)

    mock_ws.send_json.side_effect = Exception("Connection closed")

    await manager.connect(mock_ws)
    assert len(manager.active_connections) == 1

    await manager.broadcast({"message": "test"})
    assert len(manager.active_connections) == 0