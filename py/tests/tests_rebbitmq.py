import json
import pytest
import pytest_asyncio
from typing import AsyncIterator, Dict, Any
from unittest.mock import AsyncMock, patch
from src import rabbitmq


@pytest_asyncio.fixture(scope="function", autouse=True)
async def reset_rabbitmq_globals() -> AsyncIterator[None]:
    """Pytest fixture resetting global RabbitMQ state for test isolation."""
    rabbitmq.rabbitmq_connection = None
    rabbitmq.rabbitmq_channel = None
    rabbitmq.trades_queue = None
    rabbitmq.execution_reports_queue = None
    yield
    rabbitmq.rabbitmq_connection = None
    rabbitmq.rabbitmq_channel = None
    rabbitmq.trades_queue = None
    rabbitmq.execution_reports_queue = None


@pytest_asyncio.fixture(scope="function")
async def mock_channel() -> AsyncIterator[AsyncMock]:
    """Pytest fixture mocking the global RabbitMQ channel."""
    channel_mock: AsyncMock = AsyncMock()
    channel_mock.default_exchange = AsyncMock()
    rabbitmq.rabbitmq_channel = channel_mock
    yield channel_mock


@pytest.mark.asyncio
async def test_init_rabbitmq() -> None:
    """Test verifying RabbitMQ initialization using mocked connection and queues."""
    with patch("src.rabbitmq.aio_pika.connect_robust", new_callable=AsyncMock) as mock_connect:
        mock_conn: AsyncMock = AsyncMock()
        mock_channel_instance: AsyncMock = AsyncMock()
        mock_queue: AsyncMock = AsyncMock()

        mock_connect.return_value = mock_conn
        mock_conn.channel.return_value = mock_channel_instance
        mock_channel_instance.declare_queue.return_value = mock_queue

        await rabbitmq.init_rabbitmq()

        mock_connect.assert_awaited_once()
        mock_conn.channel.assert_awaited_once()

        assert mock_channel_instance.declare_queue.call_count == 3

        assert rabbitmq.rabbitmq_connection is mock_conn
        assert rabbitmq.rabbitmq_channel is mock_channel_instance
        assert rabbitmq.trades_queue is mock_queue
        assert rabbitmq.execution_reports_queue is mock_queue


@pytest.mark.asyncio
async def test_publish_order(mock_channel: AsyncMock) -> None:
    """Test verifying order publishing using mocked RabbitMQ message."""
    test_order_data: Dict[str, Any] = {
        "order_id": 999,
        "symbol": "ETHUSD",
        "action": "ADD"
    }

    with patch("src.rabbitmq.aio_pika.Message") as mock_message_class:
        mock_msg_instance: AsyncMock = AsyncMock()
        mock_message_class.return_value = mock_msg_instance

        await rabbitmq.publish_order(test_order_data)

        expected_body: bytes = json.dumps(test_order_data).encode("utf-8")
        mock_message_class.assert_called_once_with(body=expected_body)

        mock_channel.default_exchange.publish.assert_awaited_once_with(
            mock_msg_instance,
            routing_key="orders_queue"
        )
