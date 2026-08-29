#include "test.h"

#include "w2g/msg.h"
#include "w2g/pipe.h"

#include <string_view>

TEST(PipePing) {
  auto pipe = w2g::Pipe();
  w2g::Conn* a = pipe.r0;
  w2g::Conn* b = pipe.r1;
  EXPECT(a != nullptr);
  EXPECT(b != nullptr);

  wasigo::go([a]() -> wasigo::Task {
    auto hello = w2g::ToSlice(std::string_view("hello"));
    auto wr = co_await a->Write(hello);
    EXPECT(wr.r1.is_nil());
    EXPECT_EQ(wr.r0, 5);
    auto world = w2g::ToSlice(std::string_view(" world"));
    wr = co_await a->Write(world);
    EXPECT(wr.r1.is_nil());
    a->Close();
    co_return;
  }());

  wasigo::go([b]() -> wasigo::Task {
    auto buf = wasigo::make_slice<uint8_t>(5, 5);
    auto rr = co_await b->Read(buf);
    EXPECT(rr.r1.is_nil());
    EXPECT_EQ(rr.r0, 5);
    EXPECT(w2g::ToString(buf) == "hello");

    auto buf2 = wasigo::make_slice<uint8_t>(6, 6);
    rr = co_await b->Read(buf2);
    EXPECT(rr.r1.is_nil());
    EXPECT_EQ(rr.r0, 6);
    EXPECT(w2g::ToString(buf2) == " world");

    auto eof = co_await b->Read(buf);
    EXPECT(eof.r1 == w2g::errEOF);

    b->Close();
    auto wr = co_await b->Write(w2g::ToSlice(std::string_view("x")));
    EXPECT(wr.r1 == w2g::errClosedPipe);
    auto rd = co_await b->Read(buf);
    EXPECT(rd.r1 == w2g::errClosedPipe);
    co_return;
  }());

  wasigo::run();
}

TEST(PipeReadMsg) {
  auto pipe = w2g::Pipe();
  wasigo::go([a = pipe.r0]() -> wasigo::Task {
    auto wr = co_await a->Write(w2g::ToSlice(std::string_view("frame")));
    EXPECT(wr.r1.is_nil());
    a->Close();
    co_return;
  }());
  wasigo::go([b = pipe.r1]() -> wasigo::Task {
    auto rr = co_await b->ReadMsg();
    EXPECT(rr.r1.is_nil());
    EXPECT(w2g::ToString(rr.r0) == "frame");
    auto eof = co_await b->ReadMsg();
    EXPECT(eof.r1 == w2g::errEOF);
    co_return;
  }());
  wasigo::run();
}

TEST(PipeInvalidConn) {
  w2g::Conn c;
  wasigo::go([&c]() -> wasigo::Task {
    auto rr = co_await c.Read({});
    EXPECT(rr.r1 == w2g::errNotSupported);
    auto wr = co_await c.Write({});
    EXPECT(wr.r1 == w2g::errNotSupported);
    EXPECT(c.Close() == w2g::errNotSupported);
    co_return;
  }());
  wasigo::run();
}
