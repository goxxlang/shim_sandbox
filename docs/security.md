# Security

Threat model: a **sandboxee** (child, or WASI guest talking through `Shim` /
the bus) should not exhaust host memory, inherit host secrets/handles, or
reach files/env the ABAC engine did not allow.

This is not a full syscall filter and not Chromium’s sandbox. It is
portable isolation plus an explicit host-call gate.

## Enforced

| Boundary | Control |
|---|---|
| Child memory / CPU / wall | Job object (Windows) or `rlimit` (POSIX); defaults 256 MiB / 60 s CPU / 120 s wall |
| Child processes | `ACTIVE_PROCESS=1` unless `AllowSpawn()` |
| Child death | `KILL_ON_JOB_CLOSE` (Windows) |
| Inherited handles | Windows `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` — comms pipes only |
| Inherited environment | Allowlist + explicit `Executor` env; `W2G_S2_COMMS` cannot be overridden by `req.env` |
| POSIX extra fds | Child closes fds ≥ 3 except the two comms ends; `execve` custom envp |
| Comms DoS | TLV length &gt; 16 MiB → no allocate, channel closed |
| Shared memory | Max 64 MiB; `CopyTo`/`CopyFrom` overflow-checked |
| Bus frames | Payload ≤ 16 MiB; 64 layers; name/topic length + NUL checks |
| Shim files | ABAC + lexical canonicalize (no `/tmp/../etc` via `prefix:/tmp/`) |
| Shim read size | 16 MiB cap |
| Host process | `Shim::Exit` never `std::exit`s |

Windows still passes `Path` / `SystemRoot` so the loader can find the CRT.
That is a deliberate trade: without `Path`, MinGW-linked sandboxees fail
before `main`. Do not treat `Path` as a secret channel; do not put secrets
in the environment you allow.

## Not enforced (v1)

- Linux seccomp / landlock / user namespaces
- Windows AppContainer, network namespace, or a filesystem allowlist in the
  kernel (ABAC is in the shim, not NT)
- `AllowNetwork()` does not disable the NIC
- In-process `Pipe()` bus: peers are cooperative goroutines in one address
  space; frames are not authenticated beyond the bus `from` field the
  publisher sets
- `Buffer::data()` is an untrusted mapping; the sandboxee can race contents
- google/sandboxed-api sandbox2 policies (BPF) — out of scope by design

## Operator checklist

1. Put WASI file I/O through `w2g::Shim`, not `wasigo::os_open`.
2. Compile consumer TUs with `-DW2G_ABAC_SYSTEM=1` (and an optional
   `W2G_ABAC_POLICY_HEADER`) to allow System sandbox access. Without
   that define, Shim host calls compile to deny and never `fopen`.
3. Default deny: list `AbacAllow` for every subject/action/resource you
   intend. Use `prefix:` with a trailing slash and rely on canonicalize.
4. If the s2 policy has **any** ABAC rule, also `AbacAllow("s2", kSpawn, …)`
   for binaries you launch.
5. Keep `AllowSpawn()` off unless the sandboxee must create children.
6. Do not raise `kMaxMsgBytes` / `kMaxBufferBytes` / default memory without
   a reason.
7. Treat every `Comms` / `Buffer` byte from the child as hostile.
