/**
* @file main.cpp
* @brief Main entry point for the matching engine service.
* Responsible for RabbitMQ initialization, business logic configuration
* and starting a listening loop for new orders
*/
#include <iostream>
#include "Utils.hpp"
#include "RabbitMQPublisher.hpp"
#include "RabbitMQConsumer.hpp"
#include "MatchingEngine.hpp"
#include "models.hpp"

using namespace std;

int main() {
    load_dotenv();

    // Instantiates and connects a C++ RabbitMQ publisher.
    RabbitMQPublisher publisher;
    publisher.connect("localhost", 5672);

    // Registers a callback to publish matching engine trades.
    MatchingEngine engine;
    engine.on_trade = [&](Trade t) {
        publisher.publish_trade(t);
    };

    // Connects RabbitMQ consumer and starts processing incoming orders.
    RabbitMQConsumer consumer;
    if (consumer.connect("localhost", 5672)) {
        consumer.start_consuming(engine, publisher);
    } else {
        cerr << "Cannot start without order queue\n";
        return 1;
    }

    return 0;
}
