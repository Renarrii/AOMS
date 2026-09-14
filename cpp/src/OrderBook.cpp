#include "OrderBook.hpp"
#include <iostream>
#include <algorithm>

using namespace std;

// Matches buy orders against asks, executing trades, booking remainder.
void OrderBook::match_buy_order(Order& new_order) {
    while (!asks.empty() && new_order.quantity > 0) {
        auto best_ask_level = asks.begin();

        if (new_order.type == OrderType::LIMIT && new_order.price < best_ask_level->first) {
            break;
        }

        Order& best_ask_order = best_ask_level->second.orders.front();
        uint32_t trade_quantity = min(new_order.quantity, best_ask_order.quantity);

        new_order.quantity -= trade_quantity;
        best_ask_order.quantity -= trade_quantity;

        if (on_trade) {
            on_trade({
                new_order.order_id,
                best_ask_order.order_id,
                best_ask_level->first,
                trade_quantity
            });
        }

        if (best_ask_order.quantity == 0) {
            order_tracker.erase(best_ask_order.order_id);
            best_ask_level->second.orders.pop_front();
        }
        if (best_ask_level->second.orders.empty()) {
            asks.erase(best_ask_level);
        }
    }

    if (new_order.quantity > 0 && new_order.type == OrderType::LIMIT) {
        bids[new_order.price].orders.push_back(new_order);
        auto it = prev(bids[new_order.price].orders.end());
        order_tracker[new_order.order_id] = {Side::BUY, new_order.price, it};
    }
}

// Matches sell orders against bids, executing trades, booking remainder.
void OrderBook::match_sell_order(Order& new_order) {
    while (!bids.empty() && new_order.quantity > 0) {
        auto best_bid_level = bids.begin();

        if (new_order.type == OrderType::LIMIT && new_order.price > best_bid_level->first) {
            break;
        }

        Order& best_bid_order = best_bid_level->second.orders.front();
        uint32_t trade_quantity = min(new_order.quantity, best_bid_order.quantity);

        new_order.quantity -= trade_quantity;
        best_bid_order.quantity -= trade_quantity;

        if (on_trade) {
            on_trade({
                best_bid_order.order_id,
                new_order.order_id,
                best_bid_level->first,
                trade_quantity
            });
        }

        if (best_bid_order.quantity == 0) {
            order_tracker.erase(best_bid_order.order_id);
            best_bid_level->second.orders.pop_front();
        }
        if (best_bid_level->second.orders.empty()) {
            bids.erase(best_bid_level);
        }
    }

    if (new_order.quantity > 0 && new_order.type == OrderType::LIMIT) {
        asks[new_order.price].orders.push_back(new_order);
        auto it = prev(asks[new_order.price].orders.end());
        order_tracker[new_order.order_id] = {Side::SELL, new_order.price, it};
    }
}

// OrderBook methods for adding, canceling, and modifying resting orders.
void OrderBook::add_order(Order new_order) {
    if (new_order.side == Side::BUY) match_buy_order(new_order);
    else match_sell_order(new_order);
}

void OrderBook::cancel_order(uint64_t order_id) {
    auto it = order_tracker.find(order_id);
    if (it == order_tracker.end()) return;

    OrderLocation loc = it->second;

    if (loc.side == Side::BUY) {
        bids[loc.price].orders.erase(loc.iterator);
        if (bids[loc.price].orders.empty()) bids.erase(loc.price);
    } else {
        asks[loc.price].orders.erase(loc.iterator);
        if (asks[loc.price].orders.empty()) asks.erase(loc.price);
    }

    order_tracker.erase(it);
    cout << "Cancelled order ID: " << order_id << "\n";
}

void OrderBook::modify_order(uint64_t order_id, uint64_t new_price, uint32_t new_quantity) {
    auto it = order_tracker.find(order_id);
    if (it == order_tracker.end()) return;

    OrderLocation loc = it->second;
    Order& order = *loc.iterator;

    if (new_price == loc.price && new_quantity <= order.quantity) {
        order.quantity = new_quantity;
        cout << "Modified order ID: " << order_id << " (New quantity: " << new_quantity << ")\n";
    } else {
        Order modified_order = order;
        modified_order.price = new_price;
        modified_order.quantity = new_quantity;

        cancel_order(order_id);
        add_order(modified_order);
        cout << "Modified order ID: " << order_id << " at the end of the queue.\n";
    }
}