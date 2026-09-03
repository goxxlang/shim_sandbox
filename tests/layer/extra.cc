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
  // W2G_ABAC_SYSTEM=1 (this test binary) now does a real connect() to a
  // real host -- accept either a real success or a real network error,
  // but not the old always-fake "not supported" answer.
  EXPECT(reply_payload != w2g::kNotSupported);
  EXPECT(reply_payload.rfind("ok ", 0) == 0 || reply_payload.rfind("error:", 0) == 0);
  EXPECT(reply_id != 0);
}

TEST(DefaultStubsListenTcpUdp) {
  w2g::Bus bus;
  w2g::WasiNet wasi(&bus);
  wasi.Attach();
  auto stubs = w2g::DefaultStubs(&bus);
  stubs->Install();
  stubs->GoServe();

  // Real bind()s now -- use ephemeral ports (":0") throughout so this
  // doesn't collide with something else already listening on 8080.
  int hits = 0;
  wasigo::go([&]() -> wasigo::Task {
    auto a = co_await wasi.Listen("tcp", "127.0.0.1:0");
    EXPECT(a.r1.is_nil());
    EXPECT(a.r0.topic == w2g::kTopicListenReply);
    // Listen now leaves the socket open (a live listening handle, so
    // Accept() can be called on it later) -- "ok handle=<id> bound=..".
    EXPECT(w2g::ToString(a.r0.payload).rfind("ok handle=", 0) == 0);
    hits++;
    auto b = co_await wasi.TcpBind("0.0.0.0:0");
    EXPECT(b.r1.is_nil());
    EXPECT(b.r0.topic == w2g::kTopicTcpBindReply);
    EXPECT(w2g::ToString(b.r0.payload).rfind("ok bound=", 0) == 0);
    hits++;
    auto c = co_await wasi.UdpBind("0.0.0.0:0");
    EXPECT(c.r1.is_nil());
    EXPECT(c.r0.topic == w2g::kTopicUdpBindReply);
    EXPECT(w2g::ToString(c.r0.payload).rfind("ok bound=", 0) == 0);
    hits++;
    stubs->Close();
    co_return;
  }());
  wasigo::run();
  EXPECT_EQ(hits, 3);
}
