#include "test.h"

#include "w2g/stub.h"

TEST(StubDial) {
  w2g::Bus bus;
  bus.Attach("wasi", w2g::Side::kWasi);
  bus.Attach("gxx", w2g::Side::kGxx);
  EXPECT(bus.Subscribe("wasi", w2g::kTopicDialReply).is_nil());
  EXPECT(w2g::AttachStubs(&bus, "gxx").is_nil());

  std::string reply_topic;
  std::string reply_payload;

  wasigo::go(w2g::ServeGxx(&bus, "gxx"));
  wasigo::go([&]() -> wasigo::Task {
    auto rec = co_await bus.Recv("wasi");
    EXPECT(rec.r1.is_nil());
    reply_topic = rec.r0.topic;
    reply_payload = w2g::ToString(rec.r0.payload);
    bus.CloseLayer("gxx");
    co_return;
  }());
  wasigo::go([&]() -> wasigo::Task {
    auto err = co_await bus.Publish("wasi", w2g::kTopicDial,
                                    w2g::ToSlice(std::string_view("tcp example.com:80")));
    EXPECT(err.is_nil());
    co_return;
  }());
  wasigo::run();
  EXPECT(reply_topic == w2g::kTopicDialReply);
  EXPECT(reply_payload == w2g::kNotSupported);
}

TEST(StubListenTcpUdp) {
  w2g::Bus bus;
  bus.Attach("wasi", w2g::Side::kWasi);
  bus.Attach("gxx", w2g::Side::kGxx);
  bus.Subscribe("wasi", "*");
  w2g::AttachStubs(&bus, "gxx");

  int replies = 0;
  wasigo::go(w2g::ServeGxx(&bus, "gxx"));
  wasigo::go([&]() -> wasigo::Task {
    for (int i = 0; i < 3; ++i) {
      auto rec = co_await bus.Recv("wasi");
      EXPECT(rec.r1.is_nil());
      EXPECT(w2g::ToString(rec.r0.payload) == w2g::kNotSupported);
      replies++;
    }
    bus.CloseLayer("gxx");
    co_return;
  }());
  wasigo::go([&]() -> wasigo::Task {
    auto err = co_await bus.Publish("wasi", w2g::kTopicListen,
                                    w2g::ToSlice(std::string_view("tcp :8080")));
    EXPECT(err.is_nil());
    err = co_await bus.Publish("wasi", w2g::kTopicTcpBind,
                               w2g::ToSlice(std::string_view("0.0.0.0:0")));
    EXPECT(err.is_nil());
    err = co_await bus.Publish("wasi", w2g::kTopicUdpBind,
                               w2g::ToSlice(std::string_view("0.0.0.0:0")));
    EXPECT(err.is_nil());
    co_return;
  }());
  wasigo::run();
  EXPECT_EQ(replies, 3);
}
