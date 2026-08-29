#ifndef W2G_S2_BUFFER_H_
#define W2G_S2_BUFFER_H_

#include "w2g/s2/handle.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace w2g {
namespace s2 {

inline constexpr size_t kMaxBufferBytes = 64u << 20;

// Shared memory between host and sandboxee. Contents are untrusted.
class Buffer {
 public:
  ~Buffer();
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  static std::unique_ptr<Buffer> CreateWithSize(size_t size);
  static std::unique_ptr<Buffer> CreateFromHandle(Handle h, size_t size);

  uint8_t* data() const { return ptr_; }
  size_t size() const { return size_; }
  Handle::Native native() const { return h_.get(); }

  bool CopyTo(size_t off, const void* src, size_t n);
  bool CopyFrom(size_t off, void* dst, size_t n) const;
  bool Set(size_t i, uint8_t v);
  bool Get(size_t i, uint8_t* out) const;

 private:
  Buffer() = default;
  Handle h_;
  uint8_t* ptr_ = nullptr;
  size_t size_ = 0;
};

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_BUFFER_H_
