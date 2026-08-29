#ifndef W2G_S2_OS_H_
#define W2G_S2_OS_H_

#include "w2g/s2/handle.h"
#include "w2g/s2/limits.h"
#include "w2g/s2/policy.h"
#include "w2g/s2/result.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace w2g {
namespace s2 {
namespace os {

void CloseNative(Handle::Native n);

struct PipeEnds {
  Handle parent_r;
  Handle parent_w;
  Handle child_r;
  Handle child_w;
};

PipeEnds MakeDuplex();

bool Write(Handle::Native n, const void* p, uint32_t len);
bool Read(Handle::Native n, void* p, uint32_t len);

struct Mapping {
  Handle handle;
  uint8_t* ptr = nullptr;
  size_t size = 0;
};

bool CreateMapping(size_t size, Mapping* out);
bool MapExisting(Handle::Native n, size_t size, Mapping* out);
void Unmap(Mapping* m);

struct Spawned {
  Handle process;
  Handle job;
  uint64_t pid = 0;
};

struct SpawnReq {
  std::string path;
  std::vector<std::string> argv;
  std::vector<std::string> env;
  std::string cwd;
  Handle::Native child_r = Handle::Invalid();
  Handle::Native child_w = Handle::Invalid();
  const Limits* limits = nullptr;
  const Policy* policy = nullptr;
};

Result Spawn(const SpawnReq& req, Spawned* out);
Result Wait(Spawned* p, uint64_t timeout_ms, int* exit_code);
void Kill(Spawned* p);

bool CommsFromEnv(Handle* r, Handle* w);
std::string CommsEnvValue(Handle::Native r, Handle::Native w);

}  // namespace os
}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_OS_H_
