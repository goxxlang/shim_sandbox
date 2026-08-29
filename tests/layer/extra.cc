#include "test.h"

#include "w2g/stub.h"
#include "w2g/wasi.h"

TEST(DefaultStubsDial) {
  w2g::Bus bus;
  w2g::WasiNet wasi(&bus);
  EXPECT(wasi.Attach().is_nil());
  auto stubs = w2g::DefaultStubs(&bus);
  EXPECT(stubs->Install().is_nil());

  std::string reply_topic;
  std::string reply_payload;
  uint32_t reply_id = 0;

  stubs->GoServe();
  wasigo::go([&]() -> wasigo::Task {
    auto rec = co_await wasi.Dial("tcp", "example.com:80");
    EXPECT(rec.r1.is_nil());
    reply_topic = rec.r0.topic;
    reply_payload = w2g::ToString(rec.r0.payload);
    reply_id = rec.r0.id;
    stubs->Close();
    co_return;
  }());
  wasigo::run();
  EXPECT(reply_topic == w2g::kTopicDialReply);
  EXPECT(reply_payload == w2g::kNotSupported);
  EXPECT(reply_id != 0);
}

TEST(DefaultStubsListenTcpUdp) {
  w2g::Bus bus;
  w2g::WasiNet wasi(&bus);
  wasi.Attach();
  auto stubs = w2g::DefaultStubs(&bus);
  stubs->Install();
  stubs->GoServe();

  int hits = 0;
  wasigo::go([&]() -> wasigo::Task {
    auto a = co_await wasi.Listen("tcp", ":8080");
    EXPECT(a.r1.is_nil());
    EXPECT(a.r0.topic == w2g::kTopicListenReply);
    hits++;
    auto b = co_await wasi.TcpBind("0.0.0.0:0");
    EXPECT(b.r1.is_nil());
    EXPECT(b.r0.topic == w2g::kTopicTcpBindReply);
    hits++;
    auto c = co_await wasi.UdpBind("0.0.0.0:0");
    EXPECT(c.r1.is_nil());
    EXPECT(c.r0.topic == w2g::kTopicUdpBindReply);
    hits++;
    stubs->Close();
    co_return;
  }());
  wasigo::run();
  EXPECT_EQ(hits, 3);
}
