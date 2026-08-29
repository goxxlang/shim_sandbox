#include "w2g/s2/executor.h"

namespace w2g {
namespace s2 {

Executor::Executor(std::string path, std::vector<std::string> argv,
                   std::vector<std::string> env)
    : path_(std::move(path)), argv_(std::move(argv)), env_(std::move(env)) {
  auto pair = MakeCommsPair();
  parent_comms_ = std::move(pair.parent);
  child_r_ = std::move(pair.child_r);
  child_w_ = std::move(pair.child_w);
  if (argv_.empty()) argv_.push_back(path_);
}

}  // namespace s2
}  // namespace w2g
