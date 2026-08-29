# `w2g::s2` (clean-room sandbox2)

Namespace `w2g::s2` — **not** `sandbox2::`. Same roles as
[sandbox2](https://github.com/google/sandboxed-api/tree/main/sandboxed_api/sandbox2)
without Linux-only kernel APIs.

| Role | Type | Portable mechanism |
|---|---|---|
| Launch | `Executor` | Windows `CreateProcess`; POSIX `fork`/`execve` |
| Policy | `Policy` / `PolicyBuilder` | capabilities + ABAC (no seccomp BPF) |
| Limits | `Limits` | job object / `rlimit` |
| Run | `Sandbox` | `Run` / `RunAsync` / `AwaitResult` / `Kill` |
| Comms | `Comms` | duplex pipes, little-endian TLV |
| Shared memory | `Buffer` | `CreateFileMapping` / `mmap` |
| Sandboxee | `Client` | `W2G_S2_COMMS` (`read,write` native ids) |

## Policy

```cpp
auto policy = w2g::s2::PolicyBuilder()
    .AllowIo()
    .AllowStaticStartup()
    .AbacAllow("s2", w2g::abac::kSpawn, "*")
    .Build();
```

- `AllowSpawn()` — sandboxee may create child processes (job
  `ACTIVE_PROCESS` / no extra rlimit). Independent of host spawn.
- `AbacAllow("s2", kSpawn, path)` — host may launch that path when ABAC
  rules are present. Path is canonicalized (see [abac.md](abac.md)).
- `AllowNetwork()` is recorded; Windows does not turn off the NIC in v1.

Syscall allowlists (`AllowRead` as `__NR_read`) are **not** in this API.
They are not portable.

## Limits (defaults)

| Cap | Default | 0 means |
|---|---|---|
| Process memory | 256 MiB | unlimited |
| CPU | 60 s | unlimited |
| Wall time | 120 s | wait forever (`AwaitResult`) |
| Max files | 64 | unlimited |
| File write bytes | unset | unlimited |

Windows: `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, process/job memory, process
CPU time, `ACTIVE_PROCESS=1` unless `AllowSpawn()`. POSIX: `RLIMIT_AS` /
`CPU` / `FSIZE` / `NOFILE` in the child before `execve`.

## Comms

TLV: `tag u32 LE` + `len u32 LE` + payload. Tags: bool, u32, u64, string,
bytes.

Host will **not** allocate a payload larger than 16 MiB
(`kMaxMsgBytes`). Oversize length from the sandboxee closes the channel.

Sandboxee:

```cpp
int main() {
  w2g::s2::Client client;
  uint32_t n = 0;
  client.comms()->RecvU32(&n);
  client.comms()->SendU32(n + 1);
  return 0;
}
```

Host:

```cpp
auto exec = std::make_unique<w2g::s2::Executor>(path, std::vector<std::string>{path});
w2g::s2::Sandbox s2(std::move(exec), std::move(policy));
s2.RunAsync();
s2.comms()->SendU32(41);
uint32_t n = 0;
s2.comms()->RecvU32(&n);   // 42
auto r = s2.AwaitResult();
```

## Buffer

`Buffer::CreateWithSize` rejects 0 and sizes &gt; 64 MiB. Use `CopyTo` /
`CopyFrom` / `Get` / `Set` (overflow-checked). `data()` is a raw pointer
into untrusted shared memory — do not walk it without a bound.

## Client env

Child sees `W2G_S2_COMMS=<read>,<write>` plus an allowlisted host env
(Windows: `SystemRoot`, `Path`, `PATHEXT`, `SYSTEMDRIVE`, `windir`, CPU
count/arch). Explicit `Executor` env is merged; it cannot override
`W2G_S2_COMMS`. POSIX: `execve` with that env only (no parent `environ`).
