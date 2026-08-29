#ifndef W2G_S2_RESULT_H_
#define W2G_S2_RESULT_H_

#include <cstdint>
#include <string>

namespace w2g {
namespace s2 {

class Result {
 public:
  enum Status : int {
    kUnset = 0,
    kOk,
    kSetupError,
    kViolation,
    kSignaled,
    kTimeout,
    kExternalKill,
    kInternalError,
  };

  Result() = default;

  static Result Ok(int exit_code = 0);
  static Result Setup(const std::string& why);
  static Result Timeout();
  static Result Killed();
  static Result Signaled(int sig);
  static Result Internal(const std::string& why);

  Status final_status() const { return status_; }
  uintptr_t reason_code() const { return reason_; }
  const std::string& message() const { return message_; }
  std::string ToString() const;

  void Set(Status s, uintptr_t reason, std::string msg = {});

 private:
  Status status_ = kUnset;
  uintptr_t reason_ = 0;
  std::string message_;
};

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_RESULT_H_
