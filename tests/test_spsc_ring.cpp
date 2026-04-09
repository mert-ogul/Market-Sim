#include <gtest/gtest.h>

#include "marketsim/eventbus/events.hpp"
#include "marketsim/eventbus/spsc_ring.hpp"

#include <atomic>
#include <barrier>
#include <cstdint>
#include <thread>
#include <variant>

namespace {

using marketsim::domain::Quote;
using marketsim::domain::Tick;
using marketsim::domain::Trade;
using marketsim::eventbus::Event;
using marketsim::eventbus::OrderCancel;
using marketsim::eventbus::OrderNew;
using marketsim::eventbus::PortfolioUpdate;
using marketsim::eventbus::SpscRing;

TEST(SpscRing, PushPopBasic) {
  SpscRing<int, 8> ring;
  EXPECT_TRUE(ring.try_push(42));
  int v = 0;
  ASSERT_TRUE(ring.try_pop(v));
  EXPECT_EQ(v, 42);
}

TEST(SpscRing, EmptyPopFails) {
  SpscRing<int, 8> ring;
  int v = 0;
  EXPECT_FALSE(ring.try_pop(v));
}

TEST(SpscRing, FullPushFailsUntilPop) {
  SpscRing<int, 4> ring;
  EXPECT_TRUE(ring.try_push(1));
  EXPECT_TRUE(ring.try_push(2));
  EXPECT_TRUE(ring.try_push(3));
  EXPECT_TRUE(ring.try_push(4));
  EXPECT_FALSE(ring.try_push(5));
  int v = 0;
  ASSERT_TRUE(ring.try_pop(v));
  EXPECT_EQ(v, 1);
  EXPECT_TRUE(ring.try_push(5));
}

TEST(SpscRing, OrderPreservedSingleThreaded) {
  SpscRing<int, 16> ring;
  for (int i = 0; i < 50; ++i) {
    ASSERT_TRUE(ring.try_push(i));
    int v = -1;
    ASSERT_TRUE(ring.try_pop(v));
    EXPECT_EQ(v, i);
  }
}

TEST(SpscRing, WraparoundIndices) {
  SpscRing<std::uint64_t, 4> ring;
  for (std::uint64_t i = 0; i < 2000; ++i) {
    ASSERT_TRUE(ring.try_push(i));
    std::uint64_t v = 0;
    ASSERT_TRUE(ring.try_pop(v));
    EXPECT_EQ(v, i);
  }
}

TEST(SpscRing, EventVariantRoundTrip) {
  SpscRing<Event, 32> ring;
  Tick tick;
  tick.symbol = "DEMO";
  tick.last_price = 123;
  tick.time = 7;
  ASSERT_TRUE(ring.try_push(Event{tick}));

  Event out{Tick{}};
  ASSERT_TRUE(ring.try_pop(out));
  ASSERT_TRUE(std::holds_alternative<Tick>(out));
  EXPECT_EQ(std::get<Tick>(out).symbol, "DEMO");
  EXPECT_EQ(std::get<Tick>(out).last_price, 123);

  OrderNew on;
  on.symbol = "X";
  on.order_id = 99;
  ASSERT_TRUE(ring.try_push(Event{on}));
  ASSERT_TRUE(ring.try_pop(out));
  ASSERT_TRUE(std::holds_alternative<OrderNew>(out));
  EXPECT_EQ(std::get<OrderNew>(out).order_id, 99U);

  OrderCancel oc;
  oc.order_id = 3;
  oc.symbol = "Y";
  ASSERT_TRUE(ring.try_push(Event{oc}));
  ASSERT_TRUE(ring.try_pop(out));
  ASSERT_TRUE(std::holds_alternative<OrderCancel>(out));

  Trade tr;
  tr.symbol = "Z";
  tr.quantity = 5;
  ASSERT_TRUE(ring.try_push(Event{tr}));
  ASSERT_TRUE(ring.try_pop(out));
  ASSERT_TRUE(std::holds_alternative<Trade>(out));

  Quote q;
  q.symbol = "Q";
  q.bid_price = 10;
  ASSERT_TRUE(ring.try_push(Event{q}));
  ASSERT_TRUE(ring.try_pop(out));
  ASSERT_TRUE(std::holds_alternative<Quote>(out));

  PortfolioUpdate pu;
  pu.equity_cents = 1'000'000;
  ASSERT_TRUE(ring.try_push(Event{pu}));
  ASSERT_TRUE(ring.try_pop(out));
  ASSERT_TRUE(std::holds_alternative<PortfolioUpdate>(out));
}

TEST(SpscRing, StressProducerConsumer) {
  constexpr std::uint64_t kN = 200'000;
  SpscRing<std::uint64_t, 1024> ring;
  std::atomic<bool> consumer_ok{true};
  std::barrier sync(2);

  std::thread producer([&]() {
    sync.arrive_and_wait();
    for (std::uint64_t i = 0; i < kN; ++i) {
      while (!ring.try_push(i)) {
      }
    }
  });

  std::thread consumer([&]() {
    sync.arrive_and_wait();
    for (std::uint64_t i = 0; i < kN; ++i) {
      std::uint64_t v = 0;
      while (!ring.try_pop(v)) {
      }
      if (v != i) {
        consumer_ok = false;
      }
    }
  });

  producer.join();
  consumer.join();
  EXPECT_TRUE(consumer_ok);
}

}
