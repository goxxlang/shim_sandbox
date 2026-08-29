#include "w2g/s2/result.h"

namespace w2g {
namespace s2 {

void Result::Set(Status s, uintptr_t reason, std::string msg) {
  if (status_ != kUnset) return;
  status_ = s;
  reason_ = reason;
  message_ = std::move(msg);
}

Result Result::Ok(int exit_code) {
  Result r;
  r.Set(kOk, static_cast<uintptr_t>(exit_code));
  return r;
}

Result Result::Setup(const std::string& why) {
  Result r;
  r.Set(kSetupError, 0, why);
  return r;
}

Result Result::Timeout() {
  Result r;
  r.Set(kTimeout, 0);
  return r;
}

Result Result::Killed() {
  Result r;
  r.Set(kExternalKill, 0);
  return r;
}

Result Result::Signaled(int sig) {
  Result r;
  r.Set(kSignaled, static_cast<uintptr_t>(sig));
  return r;
}

Result Result::Internal(const std::string& why) {
  Result r;
  r.Set(kInternalError, 0, why);
  return r;
}

std::string Result::ToString() const {
  switch (status_) {
    case kUnset:
      return "UNSET";
    case kOk:
      return "OK exit=" + std::to_string(reason_);
    case kSetupError:
      return "SETUP_ERROR " + message_;
    case kViolation:
      return "VIOLATION";
    case kSignaled:
      return "SIGNALED " + std::to_string(reason_);
    case kTimeout:
      return "TIMEOUT";
    case kExternalKill:
      return "EXTERNAL_KILL";
    case kInternalError:
      return "INTERNAL_ERROR " + message_;
  }
  return "?";
}

}  // namespace s2
}  // namespace w2g
