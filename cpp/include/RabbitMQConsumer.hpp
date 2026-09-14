#pragma once
#include "RabbitMQCommon.hpp"
#include <string>

class MatchingEngine;
class RabbitMQPublisher;

class RabbitMQConsumer {
private:
    AmqpConnectionPtr conn;
    bool connected;
    std::string host_;
    int port_;

public:
    RabbitMQConsumer();
    ~RabbitMQConsumer();

    RabbitMQConsumer(const RabbitMQConsumer&) = delete;
    RabbitMQConsumer& operator=(const RabbitMQConsumer&) = delete;
    RabbitMQConsumer(RabbitMQConsumer&&) = default;
    RabbitMQConsumer& operator=(RabbitMQConsumer&&) = default;

    bool connect(const std::string& host = "localhost", int port = 5672);

    // Blocking infinite loop consuming orders, routing to engine, and publishing execution reports.
    void start_consuming(MatchingEngine& engine, RabbitMQPublisher& publisher);

    void close();
};