#include "test.h"

#include "w2g/msg.h"
#include "w2g/shim.h"

#include <cstdio>
#include <string>

TEST(SystemSandboxCompileEnabled) {
  EXPECT(w2g::kSystemSandboxEnabled);
  EXPECT(w2g::Shim::SystemEnabled());
}

TEST(AbacDefaultDeny) {
  w2g::abac::Engine e;
  EXPECT(!e.Check("wasi", w2g::abac::kOpen, "/tmp/x"));
}

TEST(AbacPathTraversalDenied) {
  w2g::abac::Engine e;
  e.Allow("wasi", w2g::abac::kOpen, "prefix:/tmp/");
  EXPECT(!e.Check("wasi", w2g::abac::kOpen, "/tmp/../etc/passwd"));
  EXPECT(!e.Check("wasi", w2g::abac::kOpen, "/tmp/foo/../../etc/passwd"));
  EXPECT(!e.Check("wasi", w2g::abac::kOpen, "/tmpfoo"));
  EXPECT(e.Check("wasi", w2g::abac::kOpen, "/tmp/a"));
}

TEST(AbacPrefixAllowDenyOverride) {
  w2g::abac::Engine e;
  e.Allow("wasi", w2g::abac::kOpen, "prefix:/tmp/");
  e.Deny("*", w2g::abac::kOpen, "prefix:/tmp/secret");
  EXPECT(e.Check("wasi", w2g::abac::kOpen, "/tmp/a"));
  EXPECT(!e.Check("wasi", w2g::abac::kOpen, "/tmp/secret/x"));
  EXPECT(!e.Check("gxx", w2g::abac::kOpen, "/tmp/a"));
  EXPECT(!e.Check("wasi", w2g::abac::kCreate, "/tmp/a"));
}

TEST(ShimOpenDenied) {
  w2g::abac::Engine e;
  e.Allow("wasi", w2g::abac::kOpen, "prefix:/no/such/");
  w2g::Shim shim(e, "wasi");
  auto r = shim.Open("C:\\Windows\\win.ini");
  EXPECT(!r.r1.is_nil());
  std::string msg = r.r1.str();
  EXPECT(msg.find("abac deny") != std::string::npos);
}

TEST(ShimReadFileAllowedTmp) {
  w2g::abac::Engine e;
  e.Allow("wasi", w2g::abac::kReadFile, "suffix:.w2g-abac-test");
  w2g::Shim shim(std::move(e), "wasi");
#ifdef _WIN32
  const char* path = "w2g-abac-test.w2g-abac-test";
#else
  const char* path = "w2g-abac-test.w2g-abac-test";
#endif
  FILE* f = std::fopen(path, "wb");
  EXPECT(f != nullptr);
  if (f) {
    std::fputs("ok", f);
    std::fclose(f);
  }
  auto r = shim.ReadFile(path);
  EXPECT(r.r1.is_nil());
  EXPECT(w2g::ToString(r.r0) == "ok");
  auto denied = shim.ReadFile("nope.txt");
  EXPECT(!denied.r1.is_nil());
  std::remove(path);
}
