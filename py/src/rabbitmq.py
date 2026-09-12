import aio_pika
import json
from typing import Optional, Dict, Any
from dotenv import load_dotenv
import os


load_dotenv()
rabbitmq_connection: Optional[aio_pika.RobustConnection] = None
rabbitmq_channel: Optional[aio_pika.Channel] = None
trades_queue: Optional[aio_pika.Queue] = None
execution_reports_queue: Optional[aio_pika.Queue] = None


async def init_rabbitmq() -> None:
    """Initializes robust RabbitMQ connection and declares durable message queues."""
    global rabbitmq_connection, rabbitmq_channel, trades_queue, execution_reports_queue

    rabbitmq_url = os.getenv("RABBITMQ_URL", "amqp://guest:guest@localhost/")

    rabbitmq_connection = await aio_pika.connect_robust(rabbitmq_url)
    rabbitmq_channel = await rabbitmq_connection.channel()

    await rabbitmq_channel.declare_queue("orders_queue", durable=True)

    trades_queue = await rabbitmq_channel.declare_queue("trades_queue", durable=True)

    execution_reports_queue = await rabbitmq_channel.declare_queue("execution_reports_queue", durable=True)


async def publish_order(order_data: Dict[str, Any]) -> None:
    """Publishes serialized order data to the RabbitMQ orders queue."""
    global rabbitmq_channel

    message_body: bytes = json.dumps(order_data).encode("utf-8")

    await rabbitmq_channel.default_exchange.publish(
        aio_pika.Message(body=message_body),
        routing_key="orders_queue"
    )