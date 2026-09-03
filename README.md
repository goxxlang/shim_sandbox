# shim_sandbox

ABAC System shim and isolation layer between **WASI / Go++**
([goxxlang/wasigoc](https://github.com/goxxlang/wasigoc), wasm32-wasip1)
and **extra G++ layers** (native C++ that would otherwise need sockets).

Formerly Wasi2G++. GitHub: [goxxlang/shim_sandbox](https://github.com/goxxlang/shim_sandbox).

The duplex contract is Go++ `Pipe()`: in-memory, synchronous, no sockets.
That is why it works both inside WASI preview 1 and on host G++.

```
WASI / Go++ layer
    │  w2g::Pipe()  (wasigoc net.Pipe contract)
    ▼
Bus (pub/sub over Pipe)
    │
    ├─ ExtraLayer Handle()          ordinary C++, no coroutines
    │       │
    │       ▼
    │  passthrough RPC  →  W2gSapiHandle (C ABI, SAPI-shaped)
    │
    └─ w2g::s2 Sandbox              clean-room sandbox2 roles
            │
            ▼
       w2g::Shim + ABAC             wasigoc os.* host surface
```

`Dial` / `Listen` / TCP bind / UDP bind speak this bus instead of
pretending preview 1 has sockets. `os.exec` / `os.user` / `syscall` /
`tls.dial` extra G++ stubs sit on the same bus. With `-DW2G_ABAC_SYSTEM=1`
at compile time (the library's own CMake default) every one of those is
backed by a real host-OS call (`src/sapi/real_win.cc`: real Winsock
connect/bind/accept, real `CreateProcess`, real user/env lookups,
`src/sapi/tls_win.cc`: a real Schannel/SSPI handshake with certificate
validation always on) -- see [docs/architecture.md](docs/architecture.md)'s
topics table. `net.dial`/`net.listen` leave the socket **open** (a real,
usable handle -- `src/sapi/handles.h` -- not just a reachability probe),
and a further ~12 handle-based topics (`net.accept`, `net.io.*`,
`os.exec.start`/`wait`/`stdout.read`, `tls.io.*`) give real read/write/
accept/wait on it. Reaching the rest of the host OS (`Shim` files/env,
s2 spawn) additionally needs a runtime ABAC allow; without the
compile-time define the shim compiles to deny and never calls `fopen`,
and every topic above goes back to answering a canned "not supported".

`src/gocvm_bridge.cc` wires those same real backends straight into real
Go++ *source* (not just hand-written C++ callers like `examples/bridge`):
wasigoc's `wasigo::gocvm::Call(topic, payload)` (its own `src/runtime.hpp`
-- the one compiler-known dispatch gate, not per-package FFI) reaches
here when a program is built with `goclang++.bat --shim-sandbox`
(`-DWASIGO_GOCVM_BRIDGE=1`), so `net`/`crypto/tls`/`os/exec`/`os/user`/
`syscall` stdlib source (real `net.Dial`+`Conn.Read`/`Write`, real
`tls.Dial` HTTPS, `exec.Cmd.Start`/`Wait` with streamed output, ...)
gets this repo's real backends, not a stub.

google/sandboxed-api (sandbox2) is Linux-only (namespaces, seccomp, ptrace).
That is a killer here (Windows + wasip1). shim_sandbox keeps the **shape**
(executor, policy, comms, limits, C ABI) and implements portable backends.
The default RPC channel is in-process passthrough (same idea as SAPI’s
`PassthroughBackend`).

Further reading:

| Doc | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Layers, headers, data flow |
| [docs/pipe-bus.md](docs/pipe-bus.md) | `Pipe()`, bus, extra G++ stubs, `WasiNet` |
| [docs/sandbox.md](docs/sandbox.md) | `w2g::s2` spawn, comms, limits, client |
| [docs/abac.md](docs/abac.md) | ABAC on the wasigoc OS shim |
| [docs/security.md](docs/security.md) | What is enforced, what is not |

## Build

Needs sibling [wasigoc](https://github.com/goxxlang/wasigoc)
(`src/runtime.hpp` and the `Pipe()` contract). CMake looks for
`../wasigoc` then `../WASIGo++`.

```
cmake -B build && cmake --build build && ctest --test-dir build
```

WASI (optional):

```
cmake -B build-wasi -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake
cmake --build build-wasi
```

C++20. Windows real backends now; POSIX real backends are not implemented
yet (`src/sapi/real_posix.cc` is an honest stub -- see architecture.md).
Tests live in `tests/` (pipe, bus, stubs, SAPI C ABI, s2 spawn/safety,
ABAC). Example: `examples/bridge` -- a WASI-side `WasiNet` client driving
all eight topics over `Pipe()`/`Bus` to the real extra-G++ side in one
native process, the same "wasigoc client, goclang++ server, one
shim_sandbox process" shape `goclang++.bat --shim-sandbox` builds.

## Layout

```
include/w2g/
  pipe.h bus.h msg.h layer.h stub.h wasi.h   # Pipe pub/sub
  sapi.h channel.h                           # SAPI-shaped C ABI + RPC
  abac.h shim.h path.h system_policy.h       # ABAC + compile-time System gate
  s2/                                        # portable sandbox2 roles
src/s2/os_win.cc  os_posix.cc                # OS backends
```

C++ namespace stays `w2g`. License: BSD-3-Clause (`LICENSE`).
