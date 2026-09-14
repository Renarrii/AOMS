#include "MatchingEngine.hpp"

using namespace std;

// Initializes order books and propagates trade callbacks.
OrderBook& MatchingEngine::get_book(const string& symbol) {
    if (order_books.find(symbol) == order_books.end()) {
        order_books[symbol].on_trade = [this](Trade t) {
            if (this->on_trade) this->on_trade(t);
        };
    }
    return order_books[symbol];
}

// Delegates order operations to corresponding symbol order books.
void MatchingEngine::add_order(Order new_order) {
    get_book(new_order.symbol).add_order(new_order);
}

void MatchingEngine::cancel_order(const string& symbol, uint64_t order_id) {
    if (order_books.count(symbol)) {
        order_books[symbol].cancel_order(order_id);
    }
}

void MatchingEngine::modify_order(const string& symbol, uint64_t order_id, uint64_t new_price, uint32_t new_quantity) {
    if (order_books.count(symbol)) {
        order_books[symbol].modify_order(order_id, new_price, new_quantity);
    }
}