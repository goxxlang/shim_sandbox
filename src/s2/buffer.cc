#include "w2g/s2/buffer.h"

#include "os.h"

#include <cstring>

namespace w2g {
namespace s2 {

Buffer::~Buffer() {
  os::Mapping m;
  m.ptr = ptr_;
  m.size = size_;
  os::Unmap(&m);
  ptr_ = nullptr;
  size_ = 0;
}

std::unique_ptr<Buffer> Buffer::CreateWithSize(size_t size) {
  if (size == 0 || size > kMaxBufferBytes) return nullptr;
  os::Mapping m;
  if (!os::CreateMapping(size, &m) || !m.ptr) return nullptr;
  auto b = std::unique_ptr<Buffer>(new Buffer());
  b->h_ = std::move(m.handle);
  b->ptr_ = m.ptr;
  b->size_ = m.size;
  return b;
}

std::unique_ptr<Buffer> Buffer::CreateFromHandle(Handle h, size_t size) {
  if (size == 0 || size > kMaxBufferBytes || !h.valid()) return nullptr;
  os::Mapping m;
  if (!os::MapExisting(h.get(), size, &m) || !m.ptr) return nullptr;
  auto b = std::unique_ptr<Buffer>(new Buffer());
  b->h_ = std::move(h);
  b->ptr_ = m.ptr;
  b->size_ = m.size;
  return b;
}

static bool Fits(size_t size, size_t off, size_t n) {
  if (n == 0) return off <= size;
  if (off > size) return false;
  return n <= size - off;
}

bool Buffer::CopyTo(size_t off, const void* src, size_t n) {
  if (!ptr_) return false;
  if (n == 0) return true;
  if (!src || !Fits(size_, off, n)) return false;
  std::memcpy(ptr_ + off, src, n);
  return true;
}

bool Buffer::CopyFrom(size_t off, void* dst, size_t n) const {
  if (!ptr_) return false;
  if (n == 0) return true;
  if (!dst || !Fits(size_, off, n)) return false;
  std::memcpy(dst, ptr_ + off, n);
  return true;
}

bool Buffer::Set(size_t i, uint8_t v) { return CopyTo(i, &v, 1); }

bool Buffer::Get(size_t i, uint8_t* out) const { return CopyFrom(i, out, 1); }

}  // namespace s2
}  // namespace w2g
