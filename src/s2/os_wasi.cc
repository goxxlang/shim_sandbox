#include "os.h"

#include "w2g/s2/comms.h"

namespace w2g {
namespace s2 {

void Handle::reset(Native) {}

namespace os {

// WASI preview 1 has no fork/exec, no real mmap, no signals, no process
// wait -- the same gap wasigoc's own stdlib documents honestly for
// os.exec/net.Dial rather than faking it (see ../../go++/FORK.md). The
// w2g::s2 executor is a *native host* concept (it spawns and supervises a
// sandboxed child process); a WASI guest can never legitimately be the
// spawner, so every call here fails closed instead of compiling in a
// POSIX/Win32 backend that cannot exist under wasm32-wasip1 (no
// signal.h/mman.h/sys/wait.h). This keeps `w2g` linkable as a WASI guest
// library (Bus/WasiNet/stubs still work -- they only need Pipe) without
// pretending the Spawn-based sandbox executor runs inside the sandbox
// itself.

void CloseNative(Handle::Native) {}

PipeEnds MakeDuplex() { return {}; }

bool Write(Handle::Native, const void*, uint32_t) { return false; }
bool Read(Handle::Native, void*, uint32_t) { return false; }

bool CreateMapping(size_t, Mapping*) { return false; }
bool MapExisting(Handle::Native, size_t, Mapping*) { return false; }
void Unmap(Mapping*) {}

Result Spawn(const SpawnReq&, Spawned*) {
  return Result::Setup("s2::Spawn: not supported under WASI preview 1 (no fork/exec)");
}

Result Wait(Spawned*, uint64_t, int*) {
  return Result::Internal("s2::Wait: not supported under WASI preview 1 (no process wait)");
}

void Kill(Spawned*) {}

bool CommsFromEnv(Handle*, Handle*) { return false; }

std::string CommsEnvValue(Handle::Native, Handle::Native) { return {}; }

}  // namespace os
}  // namespace s2
}  // namespace w2g
