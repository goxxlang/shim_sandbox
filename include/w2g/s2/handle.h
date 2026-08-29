#ifndef W2G_S2_HANDLE_H_
#define W2G_S2_HANDLE_H_

#include <cstdint>

namespace w2g {
namespace s2 {

// Native OS object: HANDLE on Windows, fd on POSIX.
class Handle {
 public:
#ifdef _WIN32
  using Native = void*;
  static Native Invalid() { return nullptr; }
#else
  using Native = int;
  static Native Invalid() { return -1; }
#endif

  Handle() = default;
  explicit Handle(Native n) : n_(n) {}
  ~Handle() { reset(); }

  Handle(Handle&& o) noexcept : n_(o.n_) { o.n_ = Invalid(); }
  Handle& operator=(Handle&& o) noexcept {
    if (this != &o) {
      reset();
      n_ = o.n_;
      o.n_ = Invalid();
    }
    return *this;
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  bool valid() const { return n_ != Invalid(); }
  Native get() const { return n_; }
  Native release() {
    Native n = n_;
    n_ = Invalid();
    return n;
  }
  void reset(Native n = Invalid());

 private:
  Native n_ = Invalid();
};

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_HANDLE_H_
