// Built with W2G_ABAC_SYSTEM=0: Shim must never reach the host.
#include "test.h"

#include "w2g/shim.h"

TEST(SystemSandboxCompileDisabled) {
  EXPECT(!w2g::kSystemSandboxEnabled);
  EXPECT(!w2g::Shim::SystemEnabled());
  w2g::abac::Engine e;
  e.Allow("wasi", w2g::abac::kOpen, "*");
  e.Allow("wasi", w2g::abac::kReadFile, "*");
  e.Allow("wasi", w2g::abac::kCreate, "*");
  w2g::Shim shim(e, "wasi");
  auto open = shim.Open("C:\\Windows\\win.ini");
  EXPECT(!open.r1.is_nil());
  std::string msg = open.r1.str();
  EXPECT(msg.find("compile time") != std::string::npos);
  auto rf = shim.ReadFile("C:\\Windows\\win.ini");
  EXPECT(!rf.r1.is_nil());
  EXPECT(rf.r1.str().find("compile time") != std::string::npos);
}

int g_failures = 0;

int main() {
  for (const auto& t : Tests()) t.fn();
  if (g_failures) {
    std::fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  return 0;
}
