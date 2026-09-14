#pragma once
#include <rabbitmq-c/amqp.h>
#include <memory>
#include <type_traits>

// Custom smart pointer for automatic AMQP connection cleanup.
struct AmqpConnectionDeleter {
    void operator()(amqp_connection_state_t state) const {
        amqp_destroy_connection(state);
    }
};

using AmqpConnectionPtr = std::unique_ptr<
    std::remove_pointer_t<amqp_connection_state_t>,
    AmqpConnectionDeleter
>;