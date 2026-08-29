#ifndef W2G_S2_LIMITS_H_
#define W2G_S2_LIMITS_H_

#include <cstdint>

namespace w2g {
namespace s2 {

// Resource caps applied by the OS backend (job object / rlimit).
// 0 = unlimited. Defaults cap the sandboxee so it cannot exhaust the host.
inline constexpr uint64_t kDefaultMemoryBytes = 256ULL << 20;
inline constexpr uint64_t kDefaultCpuSeconds = 60;
inline constexpr uint64_t kDefaultWallMs = 120000;
inline constexpr uint64_t kDefaultMaxFiles = 64;

class Limits {
 public:
  Limits& set_memory_bytes(uint64_t n) {
    memory_bytes_ = n;
    return *this;
  }
  Limits& set_cpu_seconds(uint64_t n) {
    cpu_seconds_ = n;
    return *this;
  }
  Limits& set_wall_ms(uint64_t n) {
    wall_ms_ = n;
    return *this;
  }
  Limits& set_file_bytes(uint64_t n) {
    file_bytes_ = n;
    return *this;
  }
  Limits& set_max_files(uint64_t n) {
    max_files_ = n;
    return *this;
  }

  uint64_t memory_bytes() const { return memory_bytes_; }
  uint64_t cpu_seconds() const { return cpu_seconds_; }
  uint64_t wall_ms() const { return wall_ms_; }
  uint64_t file_bytes() const { return file_bytes_; }
  uint64_t max_files() const { return max_files_; }

 private:
  uint64_t memory_bytes_ = kDefaultMemoryBytes;
  uint64_t cpu_seconds_ = kDefaultCpuSeconds;
  uint64_t wall_ms_ = kDefaultWallMs;
  uint64_t file_bytes_ = 0;
  uint64_t max_files_ = kDefaultMaxFiles;
};

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_LIMITS_H_
