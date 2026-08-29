#ifndef W2G_ABAC_H_
#define W2G_ABAC_H_

#include <string>
#include <string_view>
#include <vector>

namespace w2g {
namespace abac {

enum class Effect { kDeny, kAllow };

// WASIGo++ host shim surface (runtime.hpp os_* / File).
inline constexpr const char* kOpen = "os.Open";
inline constexpr const char* kCreate = "os.Create";
inline constexpr const char* kReadFile = "os.ReadFile";
inline constexpr const char* kWriteFile = "os.WriteFile";
inline constexpr const char* kGetenv = "os.Getenv";
inline constexpr const char* kExit = "os.Exit";
inline constexpr const char* kArgs = "os.Args";
inline constexpr const char* kRead = "File.Read";
inline constexpr const char* kWrite = "File.Write";
inline constexpr const char* kClose = "File.Close";
inline constexpr const char* kSpawn = "s2.Spawn";
inline constexpr const char* kDial = "net.Dial";
inline constexpr const char* kListen = "net.Listen";
inline constexpr const char* kExec = "os.Exec";
inline constexpr const char* kUser = "os.User";
inline constexpr const char* kSyscall = "syscall";
inline constexpr const char* kTlsDial = "tls.Dial";

struct Rule {
  std::string subject;   // "*" or layer name
  std::string action;    // shim op, or "*"
  std::string resource;  // "*", exact, "prefix:", or "suffix:"
  Effect effect = Effect::kDeny;
};

class Engine {
 public:
  Engine& Allow(std::string subject, std::string action, std::string resource);
  Engine& Deny(std::string subject, std::string action, std::string resource);
  // Deny-override, default deny.
  bool Check(std::string_view subject, std::string_view action,
             std::string_view resource) const;
  const std::vector<Rule>& rules() const { return rules_; }

 private:
  static bool Match(std::string_view pat, std::string_view val, bool path);
  std::vector<Rule> rules_;
};

}  // namespace abac
}  // namespace w2g

#endif  // W2G_ABAC_H_
