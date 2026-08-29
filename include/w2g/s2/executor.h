#ifndef W2G_S2_EXECUTOR_H_
#define W2G_S2_EXECUTOR_H_

#include "w2g/s2/comms.h"
#include "w2g/s2/limits.h"

#include <string>
#include <vector>

namespace w2g {
namespace s2 {

class Sandbox;

class Executor {
 public:
  Executor(std::string path, std::vector<std::string> argv,
           std::vector<std::string> env = {});
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  Limits* limits() { return &limits_; }
  Comms* comms() { return &parent_comms_; }

  Executor& set_cwd(std::string cwd) {
    cwd_ = std::move(cwd);
    return *this;
  }
  Executor& set_enable_sandbox_before_exec(bool v) {
    sandbox_before_exec_ = v;
    return *this;
  }

 private:
  friend class Sandbox;

  std::string path_;
  std::vector<std::string> argv_;
  std::vector<std::string> env_;
  std::string cwd_;
  Limits limits_;
  bool sandbox_before_exec_ = true;
  Comms parent_comms_;
  Handle child_r_;
  Handle child_w_;
};

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_EXECUTOR_H_
