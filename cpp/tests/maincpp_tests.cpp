#include <gtest/gtest.h>
#include <vector>
#include "MatchingEngine.hpp"
#include "models.hpp"

// Test fixture capturing executed trades from the matching engine.
class MatchingEngineTest : public ::testing::Test {
protected:
    MatchingEngine engine;
    std::vector<Trade> executed_trades;

    void SetUp() override {
        engine.on_trade = [this](Trade t) {
            executed_trades.push_back(t);
        };
    }
};

// Test verifying exact order match and executed trade details.
TEST_F(MatchingEngineTest, ExactMatch) {
    Order buy_order{1, "BTCUSD", Side::BUY, OrderType::LIMIT, 50000, 10, 1000};
    Order sell_order{2, "BTCUSD", Side::SELL, OrderType::LIMIT, 50000, 10, 1001};

    engine.add_order(buy_order);
    engine.add_order(sell_order);

    ASSERT_EQ(executed_trades.size(), 1);
    EXPECT_EQ(executed_trades[0].buyer_order_id, 1);
    EXPECT_EQ(executed_trades[0].seller_order_id, 2);
    EXPECT_EQ(executed_trades[0].price, 50000);
    EXPECT_EQ(executed_trades[0].quantity, 10);
}

// Test verifying partial order fills in the matching engine.
TEST_F(MatchingEngineTest, PartialFill) {
    Order buy_order{1, "BTCUSD", Side::BUY, OrderType::LIMIT, 50000, 10, 1000};
    Order sell_order{2, "BTCUSD", Side::SELL, OrderType::LIMIT, 50000, 4, 1001};

    engine.add_order(buy_order);
    engine.add_order(sell_order);

    ASSERT_EQ(executed_trades.size(), 1);
    EXPECT_EQ(executed_trades[0].quantity, 4);

    Order sell_order_2{3, "BTCUSD", Side::SELL, OrderType::LIMIT, 50000, 6, 1002};
    engine.add_order(sell_order_2);

    ASSERT_EQ(executed_trades.size(), 2);
    EXPECT_EQ(executed_trades[1].buyer_order_id, 1);
    EXPECT_EQ(executed_trades[1].seller_order_id, 3);
    EXPECT_EQ(executed_trades[1].quantity, 6);
}

// Test verifying market order fills at best limit prices.
TEST_F(MatchingEngineTest, MarketOrderFillsAtBestPrice) {
    Order sell_1{1, "BTCUSD", Side::SELL, OrderType::LIMIT, 51000, 5, 1000};
    Order sell_2{2, "BTCUSD", Side::SELL, OrderType::LIMIT, 52000, 5, 1001};

    engine.add_order(sell_1);
    engine.add_order(sell_2);

    Order market_buy{3, "BTCUSD", Side::BUY, OrderType::MARKET, 0, 7, 1002};
    engine.add_order(market_buy);

    ASSERT_EQ(executed_trades.size(), 2);

    EXPECT_EQ(executed_trades[0].price, 51000);
    EXPECT_EQ(executed_trades[0].quantity, 5);

    EXPECT_EQ(executed_trades[1].price, 52000);
    EXPECT_EQ(executed_trades[1].quantity, 2);
}

// Test verifying order cancellation prevents trade execution.
TEST_F(MatchingEngineTest, CancelOrder) {
    Order buy_order{1, "BTCUSD", Side::BUY, OrderType::LIMIT, 50000, 10, 1000};
    engine.add_order(buy_order);

    engine.cancel_order("BTCUSD", 1);

    Order sell_order{2, "BTCUSD", Side::SELL, OrderType::LIMIT, 50000, 10, 1001};
    engine.add_order(sell_order);

    EXPECT_EQ(executed_trades.size(), 0);
}

// Test verifying order quantity modification reduces trade size.
TEST_F(MatchingEngineTest, ModifyOrderDecreaseQuantity) {
    Order buy_order{1, "BTCUSD", Side::BUY, OrderType::LIMIT, 50000, 10, 1000};
    engine.add_order(buy_order);

    engine.modify_order("BTCUSD", 1, 50000, 5);

    Order sell_order{2, "BTCUSD", Side::SELL, OrderType::LIMIT, 50000, 10, 1001};
    engine.add_order(sell_order);

    ASSERT_EQ(executed_trades.size(), 1);
    EXPECT_EQ(executed_trades[0].quantity, 5);
}

// Test verifying no match when prices do not overlap.
TEST_F(MatchingEngineTest, NoMatchWhenPricesDoNotOverlap) {
    Order buy_order{1, "BTCUSD", Side::BUY, OrderType::LIMIT, 49000, 10, 1000};
    Order sell_order{2, "BTCUSD", Side::SELL, OrderType::LIMIT, 50000, 10, 1001};

    engine.add_order(buy_order);
    engine.add_order(sell_order);

    EXPECT_EQ(executed_trades.size(), 0);
}


