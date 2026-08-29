#ifndef W2G_S2_SANDBOX_H_
#define W2G_S2_SANDBOX_H_

#include "w2g/s2/executor.h"
#include "w2g/s2/handle.h"
#include "w2g/s2/policy.h"
#include "w2g/s2/result.h"

#include <memory>
#include <string>

namespace w2g {
namespace s2 {

class Sandbox {
 public:
  Sandbox(std::unique_ptr<Executor> executor, std::unique_ptr<Policy> policy);
  Sandbox(const Sandbox&) = delete;
  Sandbox& operator=(const Sandbox&) = delete;
  ~Sandbox();

  Result Run();
  bool RunAsync();
  Result AwaitResult();
  void Kill();
  bool IsTerminated() const;

  Comms* comms();
  uint64_t pid() const { return pid_; }

 private:
  std::unique_ptr<Executor> executor_;
  std::unique_ptr<Policy> policy_;
  Handle process_;
  Handle job_;
  uint64_t pid_ = 0;
  Result result_;
  bool started_ = false;
};

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_SANDBOX_H_
