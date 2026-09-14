#pragma once
#include "models.hpp"
#include <map>
#include <unordered_map>
#include <functional>

class OrderBook {
private:
    // Buy side: highest prices are at the beginning.
    std::map<uint64_t, PriceLevel, std::greater<uint64_t>> bids;
    // Sell side: lowest prices are at the beginning.
    std::map<uint64_t, PriceLevel> asks;
    // Fast O(1) order lookup by ID for cancel/modify.
    std::unordered_map<uint64_t, OrderLocation> order_tracker;

    void match_buy_order(Order& new_order);
    void match_sell_order(Order& new_order);

public:
    std::function<void(Trade)> on_trade;

    void add_order(Order new_order);
    void cancel_order(uint64_t order_id);
    void modify_order(uint64_t order_id, uint64_t new_price, uint32_t new_quantity);
};