#pragma once
#include "OrderBook.hpp"
#include "models.hpp"
#include <unordered_map>
#include <string>
#include <functional>
#include <cstdint>

// Matching engine managing multiple order books and trade callbacks.
class MatchingEngine {
private:
    std::unordered_map<std::string, OrderBook> order_books;

public:
    std::function<void(Trade)> on_trade;

    OrderBook& get_book(const std::string& symbol);
    void add_order(Order new_order);
    void cancel_order(const std::string& symbol, uint64_t order_id);
    void modify_order(const std::string& symbol, uint64_t order_id, uint64_t new_price, uint32_t new_quantity);
};