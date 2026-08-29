#ifndef W2G_PIPE_H_
#define W2G_PIPE_H_

// Common interface between WASI layers and extra G++ layers.
//
// This is WASIGo++ net.Pipe(): an in-memory, synchronous, full-duplex
// Conn pair. It needs no sockets, so it works on wasm32-wasip1 and on
// host G++. Dial/Listen/sockets stay stubbed; they talk through this
// instead (see w2g/bus.h and w2g/stub.h).

#include "runtime.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace w2g {

struct Conn;

struct Conn_ReadResult {
  int64_t r0{};
  wasigo::Error r1{};
};

struct Conn_WriteResult {
  int64_t r0{};
  wasigo::Error r1{};
};

struct Conn_ReadMsgResult {
  wasigo::Slice<uint8_t> r0{};
  wasigo::Error r1{};
};

struct PipeResult {
  Conn* r0{};
  Conn* r1{};
};

extern wasigo::Error errNotSupported;
extern wasigo::Error errClosedPipe;
extern wasigo::Error errEOF;

struct Conn {
  bool valid{};
  bool closed{};
  wasigo::Chan<wasigo::Slice<uint8_t>> recv{};
  wasigo::Chan<wasigo::Slice<uint8_t>> send{};
  wasigo::Slice<uint8_t> leftover{};

  wasigo::TaskT<Conn_ReadResult> Read(wasigo::Slice<uint8_t> p) {
    Conn* c = this;
    if (!c->valid) {
      co_return {0LL, errNotSupported};
    }
    if (c->closed) {
      co_return {0LL, errClosedPipe};
    }
    if (wasigo::len(c->leftover) == 0LL) {
      auto t = co_await (c->recv).recv_ok();
      auto chunk = std::move(t.first);
      auto ok = t.second;
      if (!ok) {
        co_return {0LL, errEOF};
      }
      c->leftover = chunk;
    }
    int64_t n = wasigo::copy(p, c->leftover);
    c->leftover = (c->leftover).slice(n, -1);
    co_return {n, {}};
  }

  // One whole Pipe write. Pub/sub uses this: a Write is one message.
  wasigo::TaskT<Conn_ReadMsgResult> ReadMsg() {
    Conn* c = this;
    if (!c->valid) {
      co_return {{}, errNotSupported};
    }
    if (c->closed) {
      co_return {{}, errClosedPipe};
    }
    if (wasigo::len(c->leftover) == 0LL) {
      auto t = co_await (c->recv).recv_ok();
      auto chunk = std::move(t.first);
      auto ok = t.second;
      if (!ok) {
        co_return {{}, errEOF};
      }
      c->leftover = chunk;
    }
    auto msg = c->leftover;
    c->leftover = {};
    co_return {msg, {}};
  }

  wasigo::TaskT<Conn_WriteResult> Write(wasigo::Slice<uint8_t> p) {
    Conn* c = this;
    if (!c->valid) {
      co_return {0LL, errNotSupported};
    }
    if (c->closed) {
      co_return {0LL, errClosedPipe};
    }
    wasigo::Slice<uint8_t> cp =
        wasigo::make_slice<uint8_t>(wasigo::len(p), wasigo::len(p));
    wasigo::copy(cp, p);
    co_await (c->send).send(cp);
    co_return {wasigo::len(p), {}};
  }

  wasigo::Error Close() {
    Conn* c = this;
    if (!c->valid) {
      return errNotSupported;
    }
    if (c->closed) {
      return {};
    }
    c->closed = true;
    wasigo::close(c->send);
    return {};
  }
};

// Two Conns connected to each other: a write on one side is delivered
// whole to a Read on the other (unbuffered -- Write blocks until the
// peer reads it). Closing either end delivers EOF to the peer's next
// Read once leftover is drained.
inline PipeResult Pipe() {
  wasigo::Chan<wasigo::Slice<uint8_t>> ab = wasigo::make_chan<wasigo::Slice<uint8_t>>(0);
  wasigo::Chan<wasigo::Slice<uint8_t>> ba = wasigo::make_chan<wasigo::Slice<uint8_t>>(0);
  Conn* a = [&] {
    auto* p = wasigo::New<Conn>();
    p->valid = true;
    p->recv = ba;
    p->send = ab;
    return p;
  }();
  Conn* b = [&] {
    auto* p = wasigo::New<Conn>();
    p->valid = true;
    p->recv = ab;
    p->send = ba;
    return p;
  }();
  return {a, b};
}

}  // namespace w2g

#endif  // W2G_PIPE_H_
