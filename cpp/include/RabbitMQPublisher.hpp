#pragma once
#include "RabbitMQCommon.hpp"
#include "models.hpp"
#include <string>
#include <cstdint>

class RabbitMQPublisher {
private:
    AmqpConnectionPtr conn;
    bool connected;

public:
    RabbitMQPublisher();
    ~RabbitMQPublisher();

    RabbitMQPublisher(const RabbitMQPublisher&) = delete;
    RabbitMQPublisher& operator=(const RabbitMQPublisher&) = delete;
    RabbitMQPublisher(RabbitMQPublisher&&) = default;
    RabbitMQPublisher& operator=(RabbitMQPublisher&&) = default;

    bool connect(const std::string& host = "localhost", int port = 5672);
    void publish_trade(const Trade& t);
    void publish_execution_report(uint64_t order_id, const std::string& status, const std::string& message = "");
    void close();
};