# Pipe and bus

## `w2g::Pipe()`

Same contract as WASIGo++ `net.Pipe()` (`stdlib/net/net.go` and generated
`net::Pipe`):

- Two `Conn`s, crossed unbuffered channels of byte slices
- A `Write` is delivered **whole** to the peer (`Read` / `ReadMsg`)
- `Write` blocks until the peer reads
- `Close` on one end: peer `Read` gets EOF after leftover is drained;
  further I/O on the closed end is `errClosedPipe`
- A default-constructed `Conn` is invalid (`errNotSupported`) — same as a
  stub `Dial` result that never connected

`ReadMsg()` consumes one channel item (one Pipe write). Pub/sub uses that.

Headers: `include/w2g/pipe.h`. Implementation uses `wasigo::Chan` /
`wasigo::go` / `wasigo::run`.

## Framing

`include/w2g/msg.h`. One Pipe write = one `Msg`:

```
ver=1 | from_len u16 LE | from | topic_len u16 LE | topic | id u32 LE | payload_len u32 LE | payload
```

`id` correlates `Bus::Call` with its reply. Caps: from/topic ≤ 64 KiB,
payload ≤ 16 MiB (`kMaxPayloadBytes`).

## Bus

`include/w2g/bus.h`. Each `Attach(name, Side::kWasi | kGxx)` allocates one
`Pipe()` pair:

- layer holds `self`
- bus holds `hub`

`Publish` writes the frame on every **other** subscriber’s hub end.
`Recv` reads `self`. `Subscribe(layer, topic)`; `"*"` matches all topics.

Limits: 64 layers, names ≤ 128 bytes, topics ≤ 256 bytes, no NUL.

`Call(from, topic, reply_topic, payload)` assigns a nonzero `id`, publishes,
then `Recv`s on `from` until that `id` and reply topic match.

Pipe is unbuffered: start receivers (`wasigo::go`) **before** `Publish`,
then `wasigo::run()`.

## Extra G++ layers

`ExtraLayer::Handle` is ordinary C++. `ServeLayer` does the Pipe IO and
copies `req.id` onto the reply.

Default stubs (`DefaultStubs`): four layers, each on its own Pipe, each
calling `W2gSapiHandle` through `PassthroughChannel`.

```cpp
w2g::Bus bus;
w2g::WasiNet wasi(&bus);
wasi.Attach();                          // subscribe to *.reply
auto stubs = w2g::DefaultStubs(&bus);
stubs->Install();
stubs->GoServe();
wasigo::go([&]() -> wasigo::Task {
  auto rec = co_await wasi.Dial("tcp", "example.com:80");
  // rec.r0.topic == net.dial.reply
  stubs->Close();
  co_return;
}());
wasigo::run();
```

`WasiNet` is the WASI-facing caller: it does not return a local
“not supported” without talking to anyone.

## SAPI-shaped C ABI

`W2gSapiHandle` in `include/w2g/sapi.h` is the extra-G++ library surface
`add_sapi_library()` would wrap. Known stub topics fill `reply_topic` +
payload and return `W2G_RESULT_UNIMPLEMENTED`. Unknown → `NOT_FOUND`.

Host code must not depend on google/sandboxed-api. Default
`PassthroughChannel` calls the C function in-process on every OS.
