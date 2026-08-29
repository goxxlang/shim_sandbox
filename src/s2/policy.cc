#include "w2g/s2/policy.h"

namespace w2g {
namespace s2 {

std::unique_ptr<Policy> PolicyBuilder::Build() {
  return std::make_unique<Policy>(p_);
}

}  // namespace s2
}  // namespace w2g
