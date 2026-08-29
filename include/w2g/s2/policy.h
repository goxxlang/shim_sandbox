#ifndef W2G_S2_POLICY_H_
#define W2G_S2_POLICY_H_

#include "w2g/abac.h"

#include <memory>
#include <string>
#include <vector>

namespace w2g {
namespace s2 {

// Portable policy. Backends enforce what the OS can: job/rlimit, no-child,
// optional no-network. WASIGo++ shim ops are gated by ABAC (default deny).
class Policy {
 public:
  bool allow_network() const { return allow_network_; }
  bool allow_spawn() const { return allow_spawn_; }
  bool allow_io() const { return allow_io_; }
  const std::vector<std::string>& files() const { return files_; }
  const std::vector<std::string>& directories() const { return directories_; }
  const abac::Engine& abac() const { return abac_; }
  abac::Engine* abac_mut() { return &abac_; }

  bool CheckShim(std::string_view subject, std::string_view action,
                 std::string_view resource) const {
    return abac_.Check(subject, action, resource);
  }

 private:
  friend class PolicyBuilder;
  bool allow_network_ = false;
  bool allow_spawn_ = false;
  bool allow_io_ = false;
  std::vector<std::string> files_;
  std::vector<std::string> directories_;
  abac::Engine abac_;
};

class PolicyBuilder {
 public:
  PolicyBuilder& AllowRead() { return AllowIo(); }
  PolicyBuilder& AllowWrite() { return AllowIo(); }
  PolicyBuilder& AllowOpen() { return AllowIo(); }
  PolicyBuilder& AllowExit() { return *this; }
  PolicyBuilder& AllowTime() { return *this; }
  PolicyBuilder& AllowSleep() { return *this; }
  PolicyBuilder& AllowStaticStartup() { return *this; }
  PolicyBuilder& AllowDynamicStartup() { return AllowIo(); }

  PolicyBuilder& AllowIo() {
    p_.allow_io_ = true;
    return *this;
  }
  PolicyBuilder& AllowNetwork() {
    p_.allow_network_ = true;
    return *this;
  }
  PolicyBuilder& AllowSpawn() {
    p_.allow_spawn_ = true;
    return *this;
  }
  PolicyBuilder& AddFile(std::string path) {
    p_.files_.push_back(std::move(path));
    return *this;
  }
  PolicyBuilder& AddDirectory(std::string path) {
    p_.directories_.push_back(std::move(path));
    return *this;
  }

  PolicyBuilder& AbacAllow(std::string subject, std::string action,
                           std::string resource) {
    p_.abac_.Allow(std::move(subject), std::move(action), std::move(resource));
    return *this;
  }
  PolicyBuilder& AbacDeny(std::string subject, std::string action,
                          std::string resource) {
    p_.abac_.Deny(std::move(subject), std::move(action), std::move(resource));
    return *this;
  }

  std::unique_ptr<Policy> Build();

 private:
  Policy p_;
};

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_POLICY_H_
