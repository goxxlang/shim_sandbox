# Architecture

shim_sandbox ([goxxlang/shim_sandbox](https://github.com/goxxlang/shim_sandbox))
is the G++ extra-layer side of Go++ `Pipe()`. [wasigoc](https://github.com/goxxlang/wasigoc)
already implements `net.Pipe()` in Go++ (and as generated C++). This repo
makes that same duplex the **common interface** between:

- WASI layers (wasigoc-generated / wasm32-wasip1)
- Extra G++ layers that are stubbed under WASI (no sockets)

Nothing here edits wasigoc in place. The runtime is included
(`../wasigoc/src/runtime.hpp` or `../WASIGo++/src/runtime.hpp`). The OS
shim (`os_open`, `os_create`, …) is **wrapped** by `w2g::Shim`, not forked.

## Planes

```
                    ┌─────────────────────────────────────┐
  WASI / wasigo     │  WasiNet::Dial / Publish / Call     │
                    └─────────────────┬───────────────────┘
                                      │ Pipe() pair
                    ┌─────────────────▼───────────────────┐
  Bus               │  topic + payload frames (LE)        │
                    │  fan-out to subscribers             │
                    └─────────────────┬───────────────────┘
                                      │
           ┌──────────────────────────┼──────────────────────────┐
           ▼                          ▼                          ▼
    ExtraLayer                  RpcChannel                  s2::Sandbox
    (gxx.dial, …)               Passthrough                 job / rlimit
           │                          │                          │
           ▼                          ▼                          ▼
    Handle()                    W2gSapiHandle               Shim + ABAC
    (sync C++)                  (C, sandboxee               os.Open/…
                                 surface)
```

## Contracts

| Plane | Unit of work | Blocking |
|---|---|---|
| `w2g::Pipe()` | whole `Write` → one `Read` / `ReadMsg` | unbuffered; `Write` waits for peer `Read` |
| Bus | one framed `Msg` per `Write` | same; receivers must be parked (`go` + `run`) |
| Extra G++ | `ExtraLayer::Handle(req, reply)` | synchronous callback; `ServeLayer` does Pipe IO |
| SAPI C ABI | `W2gSapiHandle(topic, req, …)` | in-process by default |
| `w2g::s2` | child process + TLV comms | `Run` / `RunAsync` + `AwaitResult` |

## Namespaces

| Namespace | Role |
|---|---|
| `w2g` | Pipe, bus, stubs, shim |
| `w2g::abac` | deny-override engine |
| `w2g::s2` | sandbox (not Google’s `sandbox2::`) |
| `wasigo` | WASIGo++ runtime (channels, slices, `os_*`) |

## Topics (stubbed net)

| Request | Reply | Extra layer |
|---|---|---|
| `net.dial` | `net.dial.reply` | `gxx.dial` |
| `net.listen` | `net.listen.reply` | `gxx.listen` |
| `net.tcp.bind` | `net.tcp.bind.reply` | `gxx.tcp` |
| `net.udp.bind` | `net.udp.bind.reply` | `gxx.udp` |
| `os.exec` | `os.exec.reply` | `gxx.exec` |
| `os.user` | `os.user.reply` | `gxx.user` |
| `syscall` | `syscall.reply` | `gxx.syscall` |
| `tls.dial` | `tls.dial.reply` | `gxx.tls` |

Payload of a successful stub reply is the WASIGo++ not-supported string.
The communication is real; the sockets are not.
