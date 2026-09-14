#include "RabbitMQPublisher.hpp"
#include "json.hpp"
#include <rabbitmq-c/tcp_socket.h>
#include <iostream>
#include <cstdlib>
#include <chrono>

using namespace std;
using json = nlohmann::json;

// Initializes RabbitMQ publisher defaults and ensures connection cleanup.
RabbitMQPublisher::RabbitMQPublisher() : connected(false), conn(nullptr) {}

RabbitMQPublisher::~RabbitMQPublisher() {
    close();
}

// Establishes RabbitMQ connection, authenticates, and opens channel for publishing.
bool RabbitMQPublisher::connect(const string& host, int port) {
    conn.reset(amqp_new_connection());
    amqp_socket_t *socket = amqp_tcp_socket_new(conn.get());
    if (!socket) {
        cerr << "Error creating RabbitMQ socket.\n";
        return false;
    }

    if (amqp_socket_open(socket, host.c_str(), port) != 0) {
        cerr << "Failed to connect to " << host << ":" << port << "\n";
        conn.reset();
        return false;
    }

    const char* env_user = std::getenv("RABBITMQ_USER");
    const char* env_pass = std::getenv("RABBITMQ_PASS");
    const char* rmq_user = env_user ? env_user : "guest";
    const char* rmq_pass = env_pass ? env_pass : "guest";

    amqp_login(conn.get(), "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, rmq_user, rmq_pass);
    amqp_channel_open(conn.get(), 1);

    amqp_rpc_reply_t rpc_reply = amqp_get_rpc_reply(conn.get());
    if (rpc_reply.reply_type != AMQP_RESPONSE_NORMAL) {
        cerr << "Error opening RabbitMQ channel.\n";
        return false;
    }

    connected = true;
    cout << "Successfully connected to RabbitMQ!\n";
    return true;
}

// Publishes trade details as JSON to the RabbitMQ trades queue.
void RabbitMQPublisher::publish_trade(const Trade& t) {
    if (!connected || !conn) return;

    json trade_json;
    trade_json["buyer_order_id"] = t.buyer_order_id;
    trade_json["seller_order_id"] = t.seller_order_id;
    trade_json["price"] = t.price;
    trade_json["quantity"] = t.quantity;
    trade_json["status"] = "Done";

    string message_body = trade_json.dump();

    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = 2;

    int status = amqp_basic_publish(
        conn.get(), 1,
        amqp_cstring_bytes(""),
        amqp_cstring_bytes("trades_queue"),
        0, 0, &props,
        amqp_cstring_bytes(message_body.c_str())
    );

    if (status == AMQP_STATUS_OK) cout << "Sent Trade JSON: " << message_body << "\n";
}

// Publishes order execution reports as JSON to RabbitMQ queue.
void RabbitMQPublisher::publish_execution_report(uint64_t order_id, const string& status, const string& message) {
    if (!connected || !conn) return;

    json report_json;
    report_json["order_id"] = order_id;
    report_json["status"] = status;

    if (!message.empty()) {
        report_json["message"] = message;
    }

    report_json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    string message_body = report_json.dump();

    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = 2;

    int status_code = amqp_basic_publish(
        conn.get(), 1,
        amqp_cstring_bytes(""),
        amqp_cstring_bytes("execution_reports_queue"),
        0, 0, &props,
        amqp_cstring_bytes(message_body.c_str())
    );

    if (status_code == AMQP_STATUS_OK) {
        cout << "Report (" << status << ") for order " << order_id << " sent.\n";
    } else {
        cerr << "Error sending report for order: " << order_id << "\n";
    }
}

// Closes the RabbitMQ channel and connection, resetting internal state.
void RabbitMQPublisher::close() {
    if (connected && conn) {
        amqp_channel_close(conn.get(), 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(conn.get(), AMQP_REPLY_SUCCESS);
        conn.reset();
        connected = false;
    }
}