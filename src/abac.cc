#include "w2g/abac.h"

#include "w2g/path.h"

namespace w2g {
namespace abac {

Engine& Engine::Allow(std::string subject, std::string action, std::string resource) {
  rules_.push_back({std::move(subject), std::move(action), std::move(resource),
                    Effect::kAllow});
  return *this;
}

Engine& Engine::Deny(std::string subject, std::string action, std::string resource) {
  rules_.push_back({std::move(subject), std::move(action), std::move(resource),
                    Effect::kDeny});
  return *this;
}

static bool PathAction(std::string_view action) {
  return action == kOpen || action == kCreate || action == kReadFile ||
         action == kWriteFile || action == kSpawn || action == kExec;
}

bool Engine::Match(std::string_view pat, std::string_view val, bool path) {
  if (pat == "*") return true;
  if (pat.empty()) return false;
  constexpr std::string_view kPrefix = "prefix:";
  constexpr std::string_view kSuffix = "suffix:";
  if (pat.size() >= kPrefix.size() && pat.substr(0, kPrefix.size()) == kPrefix) {
    auto p = pat.substr(kPrefix.size());
    if (path) {
      std::string pc = CanonicalPath(p);
      if (pc.empty()) pc = std::string(p);
      return PrefixBound(pc, val);
    }
    return val.size() >= p.size() && val.substr(0, p.size()) == p;
  }
  if (pat.size() >= kSuffix.size() && pat.substr(0, kSuffix.size()) == kSuffix) {
    auto p = pat.substr(kSuffix.size());
    return val.size() >= p.size() && val.substr(val.size() - p.size()) == p;
  }
  if (path) {
    std::string pc = CanonicalPath(pat);
    return !pc.empty() && pc == val;
  }
  return pat == val;
}

bool Engine::Check(std::string_view subject, std::string_view action,
                   std::string_view resource) const {
  const bool path = PathAction(action);
  std::string canon;
  std::string_view res = resource;
  if (path) {
    canon = CanonicalPath(resource);
    if (canon.empty()) return false;
    res = canon;
  }
  bool allowed = false;
  for (const auto& r : rules_) {
    if (!Match(r.subject, subject, false)) continue;
    if (!Match(r.action, action, false)) continue;
    if (!Match(r.resource, res, path)) continue;
    if (r.effect == Effect::kDeny) return false;
    allowed = true;
  }
  return allowed;
}

}  // namespace abac
}  // namespace w2g
