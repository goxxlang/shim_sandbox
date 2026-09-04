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

## Topics (extra G++ layers)

The original 8 topics each have a `gxx.*` `ExtraLayer` too (reachable
through the WASI-side `WasiNet`/`Bus` pub-sub path `examples/bridge`
demonstrates, in addition to `gocvm.Call`). The newer handle-based
follow-up topics below are `gocvm.Call`-only (`src/sapi/handle.cc`'s
dispatch) -- session state (an open socket/process/TLS session) doesn't
need a second pub-sub demo path to be useful.

| Request | Reply | Extra layer | Real backend (`W2G_ABAC_SYSTEM=1`) |
|---|---|---|---|
| `net.dial` | `net.dial.reply` | `gxx.dial` | real `connect()` (Winsock); **leaves the socket open**, handed back as a handle |
| `net.listen` | `net.listen.reply` | `gxx.listen` | real `bind()` (+`listen()` for TCP only), left open as a handle |
| `net.tcp.bind` | `net.tcp.bind.reply` | `gxx.tcp` | real `bind()` (TCP), pure probe -- closed immediately |
| `net.udp.bind` | `net.udp.bind.reply` | `gxx.udp` | real `bind()` (UDP), pure probe -- closed immediately |
| `os.exec` | `os.exec.reply` | `gxx.exec` | real `CreateProcess`, captures combined stdout+stderr, waits for exit (one-shot) |
| `os.user` | `os.user.reply` | `gxx.user` | real `GetUserNameW`/`LookupAccountNameW`/`LookupAccountSidW`+`NetUserGetInfo` (current user, or `lookup <name>` / `lookupid <sid>`) |
| `syscall` | `syscall.reply` | `gxx.syscall` | real getpid/getppid/getenv/environ/chdir/kill |
| `tls.dial` | `tls.dial.reply` | `gxx.tls` | real TCP connect **and** a real Schannel/SSPI handshake (certificate + hostname validation always on), left open as a handle |

Handle-based follow-ups (`src/sapi/handles.h`'s table; no `gxx.*` layer):

| Request | Reply | Real backend |
|---|---|---|
| `net.accept` | `net.accept.reply` | real `accept()` on a `net.listen` handle |
| `net.io.read` / `write` / `close` | `....reply` | real `recv()`/`send()`/`closesocket()` on a `net.dial`/`accept` handle |
| `net.io.readfrom` / `writeto` | `....reply` | real `recvfrom()`/`sendto()`, UDP handles |
| `os.exec.start` | `os.exec.start.reply` | real `CreateProcess`, does **not** wait -- returns a process handle |
| `os.exec.wait` | `os.exec.wait.reply` | real `WaitForSingleObject` + exit code on an `os.exec.start` handle |
| `os.exec.stdout.read` | `....reply` | real `ReadFile` on the process's stdout pipe (separate from stderr, `os.exec.start` handles only) |
| `os.exec.stderr.read` | `....reply` | real `ReadFile` on the process's stderr pipe (separate from stdout) |
| `os.exec.stdin.write` | `....reply` | real `WriteFile` onto the process's stdin pipe (`os.exec.start` handles only) |
| `os.exec.stdin.close` | `....reply` | closes the process's stdin pipe, signaling EOF to the child |
| `os.exec.lookpath` | `os.exec.lookpath.reply` | real `%PATH%`/`%PATHEXT%` search via `GetEnvironmentVariableW`/`GetFileAttributesW` |
| `tls.io.read` / `write` / `close` | `....reply` | real `DecryptMessage`/`EncryptMessage` around a `tls.dial` handle's socket |

The communication (Pipe/Bus/ExtraLayer/SAPI) was always real; what used to
be fake was every reply's payload -- a canned "not supported" string,
regardless of `W2G_ABAC_SYSTEM`. `src/sapi/real_win.cc`/`tls_win.cc` (see
[pipe-bus.md](pipe-bus.md)) now does the real work when the compile-time
System gate is on (the library's own CMake default). Building without it
(`-DW2G_ABAC_SYSTEM=0`) restores the old always-"not supported" behavior --
these are still real syscalls with real, possibly sensitive, effects
(spawning processes, opening sockets, reading env/user info), gated the
same way `w2g::Shim`'s file access already was.
