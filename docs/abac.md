# ABAC and the wasigoc OS shim

wasigoc’s host OS surface lives in `runtime.hpp`: `os_open`, `os_create`,
`os_read_file`, `os_write_file`, `os_getenv`, `os_exit`, and `File`
Read/Write/Close. Generated WASI code would otherwise `fopen` with ambient
authority.

shim_sandbox does **not** patch that header. `w2g::Shim` is the gate: same
results (`OsOpenResult`, …), ABAC first.

## Engine

Deny-override, **default deny**.

```cpp
w2g::abac::Engine e;
e.Allow("wasi", w2g::abac::kOpen, "prefix:/tmp/");
e.Deny("*", w2g::abac::kOpen, "prefix:/tmp/secret");

w2g::Shim shim(e, "wasi");
shim.Open("/tmp/a");            // allow
shim.Open("/tmp/secret/x");     // deny
shim.Open("/tmp/../etc/passwd"); // deny (canonical path is /etc/passwd)
```

### Fields

| Field | Match |
|---|---|
| `subject` | `*` or exact layer name (`wasi`, `gxx.dial`, `s2`) |
| `action` | `*` or a shim op below |
| `resource` | `*`, exact, `prefix:`, or `suffix:` |

Empty pattern is **not** a wildcard.

### Path actions

These canonicalize the resource **before** matching (`w2g::CanonicalPath`):
`os.Open`, `os.Create`, `os.ReadFile`, `os.WriteFile`, `s2.Spawn`.

- `\` → `/`, lexical `.` / `..`, drive letters lowercased
- NUL / control bytes → deny
- relative `..` that would escape → deny
- `prefix:/tmp` matches `/tmp` and `/tmp/a`, not `/tmpfoo`

Non-path actions (`os.Getenv`, `net.Dial`, …) match the raw resource string.

### Actions

| Constant | Meaning |
|---|---|
| `os.Open` / `os.Create` / `os.ReadFile` / `os.WriteFile` | WASIGo++ file shim |
| `os.Getenv` / `os.Exit` / `os.Args` | env / exit / argv |
| `File.Read` / `Write` / `Close` | open file handle |
| `s2.Spawn` | host launching a sandboxee |
| `net.Dial` / `net.Listen` | bus topics for extra G++ stubs |
| `os.Exec` / `os.User` / `syscall` / `tls.Dial` | extra G++ stubs for WASIGo++ stub packages |

## Shim behavior

- `ReadFile` streams with a **16 MiB** cap (`kMaxMsgBytes`); larger files
  error instead of growing the host heap.
- `WriteFile` rejects oversized slices the same way.
- `Getenv` with an embedded NUL is denied.
- `Exit` **does not** call `std::exit`. The shim runs in the host process.

## Compile-time System sandbox

Host OS access (`fopen`, `getenv`, s2 spawn from a consumer) is **off
until the TU is compiled with `-DW2G_ABAC_SYSTEM=1`**. Without that
define, `Shim::Open`/`Create`/`ReadFile`/`WriteFile`/`Getenv` return
`kSystemDisabled` and never call the host. Runtime ABAC is still
default-deny when the compile-time gate is on.

Operator policy (optional), via `-DW2G_ABAC_POLICY_HEADER="my_policy.h"`:

```cpp
#define W2G_ABAC_APPLY(engine) do { \
    (engine).Allow("wasi", w2g::abac::kOpen, "prefix:/tmp/"); \
    (engine).Allow("s2", w2g::abac::kSpawn, "*"); \
  } while (0)
```

`Shim` constructors apply `W2G_ABAC_APPLY` to the engine. An empty
policy still default-denies every host call.

See `include/w2g/system_policy.h`. Extra G++ stubs for `os.exec`,
`os.user`, `syscall`, and `tls.dial` are on the same bus as `net.dial`;
those topics also refuse System access when the compile-time gate is off.

## Policy wiring

```cpp
PolicyBuilder()
  .AbacAllow("s2", w2g::abac::kSpawn, "*")
  .AbacAllow("wasi", w2g::abac::kOpen, "prefix:/tmp/")
  .AbacDeny("*", w2g::abac::kOpen, "prefix:/etc/");
```

If the policy has any ABAC rules, `Sandbox::RunAsync` requires
`CheckShim("s2", s2.Spawn, path)` or setup fails. Extra G++
`StubLayer::set_engine` can deny `net.dial` the same way.
