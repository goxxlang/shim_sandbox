#ifndef W2G_S2_COMMS_H_
#define W2G_S2_COMMS_H_

#include "w2g/s2/handle.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace w2g {
namespace s2 {

inline constexpr const char* kCommsEnv = "W2G_S2_COMMS";
// Host will not allocate a TLV payload larger than this (sandboxee-controlled).
inline constexpr uint32_t kMaxMsgBytes = 16u << 20;

// TLV over a byte pipe. Tags and lengths are little-endian on every OS.
class Comms {
 public:
  static constexpr uint32_t kTagBool = 1;
  static constexpr uint32_t kTagU32 = 2;
  static constexpr uint32_t kTagU64 = 3;
  static constexpr uint32_t kTagString = 4;
  static constexpr uint32_t kTagBytes = 5;

  Comms() = default;
  Comms(Handle r, Handle w) : r_(std::move(r)), w_(std::move(w)) {}
  Comms(Comms&&) = default;
  Comms& operator=(Comms&&) = default;

  bool IsConnected() const { return r_.valid() && w_.valid(); }
  void Terminate();

  Handle::Native read_native() const { return r_.get(); }
  Handle::Native write_native() const { return w_.get(); }

  void set_max_msg_bytes(uint32_t n) { max_msg_ = n ? n : kMaxMsgBytes; }
  uint32_t max_msg_bytes() const { return max_msg_; }

  bool SendTLV(uint32_t tag, const void* data, uint32_t len);
  bool RecvTLV(uint32_t* tag, std::vector<uint8_t>* value);

  bool SendU32(uint32_t v);
  bool RecvU32(uint32_t* v);
  bool SendU64(uint64_t v);
  bool RecvU64(uint64_t* v);
  bool SendBool(bool v);
  bool RecvBool(bool* v);
  bool SendString(const std::string& v);
  bool RecvString(std::string* v);
  bool SendBytes(const uint8_t* p, uint32_t n);
  bool RecvBytes(std::vector<uint8_t>* v);

 private:
  bool WriteAll(const void* p, uint32_t n);
  bool ReadAll(void* p, uint32_t n);

  Handle r_;
  Handle w_;
  uint32_t max_msg_ = kMaxMsgBytes;
};

// Two connected Comms ends (parent / child).
struct CommsPair {
  Comms parent;
  Handle child_r;
  Handle child_w;
};

CommsPair MakeCommsPair();

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_COMMS_H_
