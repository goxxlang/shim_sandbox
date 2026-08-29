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

`Dial` / `Listen` / TCP bind / UDP bind stay stubbed. They speak this bus
instead of pretending preview 1 has sockets. `os.exec` / `os.user` /
`syscall` / `tls.dial` extra G++ stubs sit on the same bus. Reaching the
host OS (`Shim` files/env, s2 spawn) requires `-DW2G_ABAC_SYSTEM=1` at
compile time plus a runtime ABAC allow; without the define the shim
compiles to deny and never calls `fopen`.

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

C++20. Windows (MSVC or MinGW) and POSIX. Tests live in `tests/` (pipe, bus,
stubs, SAPI C ABI, s2 spawn/safety, ABAC). Example: `examples/bridge` (WASI
`Dial` → `gxx.dial` stub over `Pipe()`).

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
