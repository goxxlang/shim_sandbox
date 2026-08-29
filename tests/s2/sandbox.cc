#include "test.h"

#include "w2g/s2/sandbox.h"

#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

static std::string SandboxeePath() {
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (!n) return {};
  std::string p(buf, n);
  auto slash = p.find_last_of("\\/");
  if (slash == std::string::npos) return "w2g_s2_sandboxee.exe";
  return p.substr(0, slash + 1) + "w2g_s2_sandboxee.exe";
#else
  return "w2g_s2_sandboxee";
#endif
}

TEST(S2SandboxPing) {
  std::string path = SandboxeePath();
  auto exec = std::make_unique<w2g::s2::Executor>(
      path, std::vector<std::string>{path});
  auto policy = w2g::s2::PolicyBuilder()
                    .AllowIo()
                    .AllowStaticStartup()
                    .AbacAllow("s2", w2g::abac::kSpawn, "*")
                    .Build();
  w2g::s2::Sandbox s2(std::move(exec), std::move(policy));
  EXPECT(s2.RunAsync());
  if (s2.comms()) {
    EXPECT(s2.comms()->SendU32(41));
    uint32_t n = 0;
    EXPECT(s2.comms()->RecvU32(&n));
    EXPECT_EQ(n, 42u);
  }
  auto r = s2.AwaitResult();
  EXPECT_EQ(r.final_status(), w2g::s2::Result::kOk);
}

TEST(S2PolicyBuild) {
  auto p = w2g::s2::PolicyBuilder().AllowIo().AllowNetwork().AddFile("x").Build();
  EXPECT(p->allow_io());
  EXPECT(p->allow_network());
  EXPECT(!p->allow_spawn());
  EXPECT_EQ(p->files().size(), 1u);
}
