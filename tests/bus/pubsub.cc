#include "test.h"

#include "w2g/bus.h"

TEST(BusPubSub) {
  w2g::Bus bus;
  EXPECT(bus.Attach("wasi", w2g::Side::kWasi) != nullptr);
  EXPECT(bus.Attach("gxx", w2g::Side::kGxx) != nullptr);
  EXPECT(bus.Subscribe("gxx", "ping").is_nil());

  std::string got_from;
  std::string got_topic;
  std::string got_payload;

  wasigo::go([&]() -> wasigo::Task {
    auto rec = co_await bus.Recv("gxx");
    EXPECT(rec.r1.is_nil());
    got_from = rec.r0.from;
    got_topic = rec.r0.topic;
    got_payload = w2g::ToString(rec.r0.payload);
    co_return;
  }());

  wasigo::go([&]() -> wasigo::Task {
    auto err = co_await bus.Publish("wasi", "ping", w2g::ToSlice(std::string_view("hi")));
    EXPECT(err.is_nil());
    co_return;
  }());

  wasigo::run();
  EXPECT(got_from == "wasi");
  EXPECT(got_topic == "ping");
  EXPECT(got_payload == "hi");
}

TEST(BusFanout) {
  w2g::Bus bus;
  bus.Attach("wasi", w2g::Side::kWasi);
  bus.Attach("gxx.a", w2g::Side::kGxx);
  bus.Attach("gxx.b", w2g::Side::kGxx);
  bus.Subscribe("gxx.a", "net.dial");
  bus.Subscribe("gxx.b", "net.dial");

  int hits = 0;
  wasigo::go([&]() -> wasigo::Task {
    auto rec = co_await bus.Recv("gxx.a");
    EXPECT(rec.r1.is_nil());
    EXPECT(rec.r0.topic == "net.dial");
    hits++;
    co_return;
  }());
  wasigo::go([&]() -> wasigo::Task {
    auto rec = co_await bus.Recv("gxx.b");
    EXPECT(rec.r1.is_nil());
    EXPECT(rec.r0.topic == "net.dial");
    hits++;
    co_return;
  }());
  wasigo::go([&]() -> wasigo::Task {
    auto err = co_await bus.Publish("wasi", "net.dial",
                                    w2g::ToSlice(std::string_view("tcp example.com:80")));
    EXPECT(err.is_nil());
    co_return;
  }());
  wasigo::run();
  EXPECT_EQ(hits, 2);
}

TEST(BusNoSelfDelivery) {
  w2g::Bus bus;
  bus.Attach("wasi", w2g::Side::kWasi);
  bus.Subscribe("wasi", "ping");
  bus.Attach("gxx", w2g::Side::kGxx);
  bus.Subscribe("gxx", "ping");

  int gxx_hits = 0;
  wasigo::go([&]() -> wasigo::Task {
    auto rec = co_await bus.Recv("gxx");
    EXPECT(rec.r1.is_nil());
    gxx_hits++;
    co_return;
  }());
  wasigo::go([&]() -> wasigo::Task {
    auto err = co_await bus.Publish("wasi", "ping", w2g::ToSlice(std::string_view("x")));
    EXPECT(err.is_nil());
    co_return;
  }());
  wasigo::run();
  EXPECT_EQ(gxx_hits, 1);
}

TEST(BusUnknownLayer) {
  w2g::Bus bus;
  EXPECT(!bus.Subscribe("nope", "x").is_nil());
  EXPECT(bus.Attach("wasi", w2g::Side::kWasi) != nullptr);
  EXPECT(bus.Attach("wasi", w2g::Side::kWasi) == nullptr);
}
