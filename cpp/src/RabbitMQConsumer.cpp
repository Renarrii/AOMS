#include "RabbitMQConsumer.hpp"
#include "RabbitMQPublisher.hpp"
#include "MatchingEngine.hpp"
#include "models.hpp"
#include "json.hpp"
#include <rabbitmq-c/tcp_socket.h>
#include <iostream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <stdexcept>

using namespace std;
using json = nlohmann::json;

// Initializes RabbitMQ consumer defaults and ensures connection cleanup.
RabbitMQConsumer::RabbitMQConsumer() : connected(false), conn(nullptr), host_("localhost"), port_(5672) {}

RabbitMQConsumer::~RabbitMQConsumer() {
    close();
}

// Connects to RabbitMQ, authenticates, and opens an AMQP channel.
bool RabbitMQConsumer::connect(const string& host, int port) {
    host_ = host;
    port_ = port;

    conn.reset(amqp_new_connection());
    amqp_socket_t *socket = amqp_tcp_socket_new(conn.get());

    if (!socket || amqp_socket_open(socket, host_.c_str(), port_) != 0) {
        cerr << "Error connecting consumer to RabbitMQ\n";
        conn.reset();
        return false;
    }

    const char* env_user = getenv("RABBITMQ_USER");
    const char* env_pass = getenv("RABBITMQ_PASS");
    const char* rmq_user = env_user ? env_user : "guest";
    const char* rmq_pass = env_pass ? env_pass : "guest";

    amqp_login(conn.get(), "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, rmq_user, rmq_pass);
    amqp_channel_open(conn.get(), 1);

    if (amqp_get_rpc_reply(conn.get()).reply_type != AMQP_RESPONSE_NORMAL) {
        cerr << "Error opening channel for consumer.\n";
        return false;
    }

    connected = true;
    cout << "Successfully connected to RabbitMQ (" << host_ << ":" << port_ << ")!\n";
    return true;
}

// Consumes, validates, and routes RabbitMQ orders to the matching engine.
void RabbitMQConsumer::start_consuming(MatchingEngine& engine, RabbitMQPublisher& publisher) {
    while (true) {
        if (!connected) {
            cout << "Attempting to reconnect consumer to RabbitMQ...\n";
            if (!connect(host_, port_)) {
                this_thread::sleep_for(chrono::seconds(3));
                continue;
            }
        }

        amqp_basic_consume(
            conn.get(), 1,
            amqp_cstring_bytes("orders_queue"),
            amqp_empty_bytes, 0, 1, 0, amqp_empty_table
        );

        if (amqp_get_rpc_reply(conn.get()).reply_type != AMQP_RESPONSE_NORMAL) {
            cerr << "Error registering consumer. Restarting connection...\n";
            close();
            this_thread::sleep_for(chrono::seconds(3));
            continue;
        }

        cout << "Engine started listening on 'orders_queue'...\n";
        amqp_envelope_t envelope;
        bool connection_dropped = false;

        while (true) {
            amqp_maybe_release_buffers(conn.get());
            amqp_rpc_reply_t res = amqp_consume_message(conn.get(), &envelope, NULL, 0);

            if (res.reply_type == AMQP_RESPONSE_NORMAL) {
                string payload(static_cast<char*>(envelope.message.body.bytes), envelope.message.body.len);

                try {
                    json j = json::parse(payload);

                    if (!j.contains("action") || !j["action"].is_string()) {
                        throw invalid_argument("Missing or invalid 'action' field");
                    }
                    string action = j["action"];

                    if (action == "ADD") {
                        if (!j.contains("order_id") || !j["order_id"].is_number_unsigned()) throw invalid_argument("Invalid 'order_id'");
                        if (!j.contains("symbol") || !j["symbol"].is_string() || j["symbol"].get<string>().empty()) throw invalid_argument("Invalid 'symbol'");
                        if (!j.contains("quantity") || !j["quantity"].is_number_unsigned() || j["quantity"].get<uint32_t>() == 0) throw invalid_argument("Quantity must be greater than 0");
                        if (!j.contains("side") || !j["side"].is_string()) throw invalid_argument("Missing 'side' field");
                        if (!j.contains("type") || !j["type"].is_string()) throw invalid_argument("Missing 'type' field");

                        string side_str = j["side"];
                        if (side_str != "BUY" && side_str != "SELL") throw invalid_argument("'side' field must be BUY or SELL");

                        string type_str = j["type"];
                        if (type_str != "LIMIT" && type_str != "MARKET") throw invalid_argument("'type' field must be LIMIT or MARKET");

                        uint64_t price = j.value("price", 0ULL);
                        if (type_str == "LIMIT" && price == 0) {
                            throw invalid_argument("LIMIT order must have a price greater than 0");
                        }

                        Order new_order;
                        new_order.order_id = j["order_id"];
                        new_order.symbol = j["symbol"];
                        new_order.side = (side_str == "BUY") ? Side::BUY : Side::SELL;
                        new_order.type = (type_str == "MARKET") ? OrderType::MARKET : OrderType::LIMIT;
                        new_order.price = price;
                        new_order.quantity = j["quantity"];
                        new_order.timestamp = j.value("timestamp", 0ULL);

                        cout << "Received valid ADD order (ID: " << new_order.order_id << ")\n";
                        engine.add_order(new_order);

                        publisher.publish_execution_report(new_order.order_id, "ACCEPTED");

                    } else if (action == "CANCEL") {
                        if (!j.contains("order_id") || !j["order_id"].is_number_unsigned()) throw invalid_argument("Invalid 'order_id'");
                        if (!j.contains("symbol") || !j["symbol"].is_string() || j["symbol"].get<string>().empty()) throw invalid_argument("Invalid 'symbol'");

                        uint64_t order_id = j["order_id"];
                        string symbol = j["symbol"];

                        cout << "Received valid CANCEL request (ID: " << order_id << ")\n";
                        engine.cancel_order(symbol, order_id);

                        publisher.publish_execution_report(order_id, "CANCELED");

                    } else if (action == "MODIFY") {
                        if (!j.contains("order_id") || !j["order_id"].is_number_unsigned()) throw invalid_argument("Invalid 'order_id'");
                        if (!j.contains("symbol") || !j["symbol"].is_string() || j["symbol"].get<string>().empty()) throw invalid_argument("Invalid 'symbol'");
                        if (!j.contains("new_price") || !j["new_price"].is_number_unsigned() || j["new_price"].get<uint64_t>() == 0) throw invalid_argument("New price must be greater than 0");
                        if (!j.contains("new_quantity") || !j["new_quantity"].is_number_unsigned() || j["new_quantity"].get<uint32_t>() == 0) throw invalid_argument("Quantity must be greater than 0");

                        uint64_t order_id = j["order_id"];
                        string symbol = j["symbol"];
                        uint64_t new_price = j["new_price"];
                        uint32_t new_quantity = j["new_quantity"];

                        cout << "Received valid MODIFY request (ID: " << order_id << ")\n";
                        engine.modify_order(symbol, order_id, new_price, new_quantity);

                        publisher.publish_execution_report(order_id, "MODIFIED");

                    } else {
                        throw invalid_argument("Unknown action: " + action);
                    }
                }
                catch (const invalid_argument& e) {
                    cerr << "Message rejected - Validation error: " << e.what() << "\nPayload: " << payload << "\n";
                    try {
                        json err_j = json::parse(payload);
                        if (err_j.contains("order_id") && err_j["order_id"].is_number_unsigned()) {
                            publisher.publish_execution_report(err_j["order_id"], "REJECTED", e.what());
                        }
                    } catch (...) {}
                }
                catch (const json::parse_error& e) {
                    cerr << "Message rejected - Invalid JSON format: " << e.what() << "\nPayload: " << payload << "\n";
                }
                catch (const exception& e) {
                    cerr << "Message rejected - Unexpected error: " << e.what() << "\nPayload: " << payload << "\n";
                }

                amqp_destroy_envelope(&envelope);
            }
            else {
                cerr << "Connection to RabbitMQ lost while listening.\n";
                connection_dropped = true;
                break;
            }
        }

        if (connection_dropped) {
            close();
            this_thread::sleep_for(chrono::seconds(3));
        }
    }
}

// Closes the RabbitMQ channel and connection, resetting internal state.
void RabbitMQConsumer::close() {
    if (connected && conn) {
        amqp_channel_close(conn.get(), 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(conn.get(), AMQP_REPLY_SUCCESS);
        conn.reset();
        connected = false;
        cout << "Connection to RabbitMQ closed.\n";
    }
}