#pragma once
#include <cstdint>
#include <string>
#include <list>

// Core data model definitions for the matching engine.
enum class Side { BUY, SELL };
enum class OrderType { LIMIT, MARKET };

struct Order {
    uint64_t order_id;
    std::string symbol;
    Side side;
    OrderType type;
    uint64_t price;
    uint32_t quantity;
    uint64_t timestamp;
};

struct Trade {
    uint64_t buyer_order_id;
    uint64_t seller_order_id;
    uint64_t price;
    uint32_t quantity;
};

struct PriceLevel {
    uint32_t total_volume = 0;
    std::list<Order> orders;
};

// Enables fast order cancellation and modification with O(1) complexity.
struct OrderLocation {
    Side side;
    uint64_t price;
    std::list<Order>::iterator iterator;
};