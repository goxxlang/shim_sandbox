#include "test.h"

#include "w2g/sapi.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#include <string>

// This test binary is built with W2G_ABAC_SYSTEM=1 (see CMakeLists.txt),
// so W2gSapiHandle now does the real work (src/sapi/real_win.cc) instead
// of always answering "not supported".

TEST(SapiHandleDialRefused) {
  // Port 1 (tcpmux) is essentially never listening on a dev box: a real
  // connect() to it deterministically fails, without depending on an
  // outbound network path the way dialing a real remote host would.
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[256];
  uint32_t n = sizeof(buf);
  std::string req = "tcp 127.0.0.1:1";
  W2gResult rc = W2gSapiHandle("net.dial", reinterpret_cast<const uint8_t*>(req.data()),
                               static_cast<uint32_t>(req.size()), topic, sizeof(topic), buf, &n);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  EXPECT(std::strcmp(topic, "net.dial.reply") == 0);
  std::string reply(reinterpret_cast<char*>(buf), n);
  EXPECT(reply.rfind("error:", 0) == 0);
}

TEST(SapiHandleDialInvalidAddress) {
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[256];
  uint32_t n = sizeof(buf);
  std::string req = "tcp no-port-here";
  W2gResult rc = W2gSapiHandle("net.dial", reinterpret_cast<const uint8_t*>(req.data()),
                               static_cast<uint32_t>(req.size()), topic, sizeof(topic), buf, &n);
  EXPECT_EQ(rc, W2G_RESULT_INVALID_ARGUMENT);
}

TEST(SapiHandleUser) {
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[256];
  uint32_t n = sizeof(buf);
  W2gResult rc = W2gSapiHandle("os.user", nullptr, 0, topic, sizeof(topic), buf, &n);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  EXPECT(std::strcmp(topic, "os.user.reply") == 0);
  EXPECT(n > 0);
}

TEST(SapiHandleSyscallGetpid) {
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[64];
  uint32_t n = sizeof(buf);
  std::string req = "getpid";
  W2gResult rc = W2gSapiHandle("syscall", reinterpret_cast<const uint8_t*>(req.data()),
                               static_cast<uint32_t>(req.size()), topic, sizeof(topic), buf, &n);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  std::string reply(reinterpret_cast<char*>(buf), n);
  EXPECT(!reply.empty());
  for (char c : reply) EXPECT(c >= '0' && c <= '9');
}

TEST(SapiHandleExec) {
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[512];
  uint32_t n = sizeof(buf);
  std::string req = std::string("cmd.exe") + '\x1f' + "/c" + '\x1f' + "echo real-exec-works";
  W2gResult rc = W2gSapiHandle("os.exec", reinterpret_cast<const uint8_t*>(req.data()),
                               static_cast<uint32_t>(req.size()), topic, sizeof(topic), buf, &n);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  std::string reply(reinterpret_cast<char*>(buf), n);
  EXPECT(reply.find("real-exec-works") != std::string::npos);
}

namespace {
std::string Call(const char* topic, const std::string& req, W2gResult* rc_out) {
  char topic_buf[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[1 << 16];
  uint32_t n = sizeof(buf);
  *rc_out = W2gSapiHandle(topic, reinterpret_cast<const uint8_t*>(req.data()),
                          static_cast<uint32_t>(req.size()), topic_buf, sizeof(topic_buf), buf, &n);
  return std::string(reinterpret_cast<char*>(buf), n);
}
std::string HandleOf(const std::string& reply) {
  auto pos = reply.find("handle=");
  if (pos == std::string::npos) return "";
  pos += 7;
  auto end = reply.find(' ', pos);
  return reply.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}
}  // namespace

// Real handshake against a real host -- network-dependent, same as
// DefaultStubsDial above. Automatic certificate validation is never
// disabled (see tls_win.cc), so this also exercises that a *trusted*
// real-world cert is accepted, not just that a handshake completes.
TEST(SapiHandleTlsRealHandshakeAndHttpGet) {
  W2gResult rc;
  std::string dial = Call("tls.dial", "example.com:443", &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  std::string h = HandleOf(dial);
  EXPECT(!h.empty());
  if (h.empty()) return;

  std::string req = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
  std::string wr = Call("tls.io.write", h + '\x1f' + req, &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  EXPECT_EQ(wr, std::to_string(req.size()));

  std::string resp;
  for (int i = 0; i < 20; ++i) {
    std::string chunk = Call("tls.io.read", h + "\x1f" + "4096", &rc);
    EXPECT_EQ(rc, W2G_RESULT_OK);
    if (chunk.empty()) break;  // EOF
    resp += chunk;
    if (resp.find("</html>") != std::string::npos) break;
  }
  EXPECT(resp.rfind("HTTP/1.1 200", 0) == 0);

  Call("tls.io.close", h, &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
}

// Deterministic (loopback, no external network): a real bind+listen,
// real accept, real connected client, and a real byte round trip.
TEST(SapiHandleNetLoopbackRoundTrip) {
  W2gResult rc;
  std::string listen = Call("net.listen", "tcp 127.0.0.1:0", &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  std::string lh = HandleOf(listen);
  EXPECT(!lh.empty());
  auto bound_pos = listen.find("bound=");
  EXPECT(bound_pos != std::string::npos);
  std::string addr = listen.substr(bound_pos + 6);

  std::string dial = Call("net.dial", "tcp " + addr, &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  std::string ch = HandleOf(dial);
  EXPECT(!ch.empty());

  std::string accept = Call("net.accept", lh, &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  std::string sh = HandleOf(accept);
  EXPECT(!sh.empty());

  std::string msg = "hello over a real socket";
  std::string wr = Call("net.io.write", ch + '\x1f' + msg, &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  EXPECT_EQ(wr, std::to_string(msg.size()));

  std::string got = Call("net.io.read", sh + "\x1f" + "64", &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  EXPECT(got == msg);

  Call("net.io.close", ch, &rc);
  Call("net.io.close", sh, &rc);
  Call("net.io.close", lh, &rc);
}

// Deterministic: real Start/stdout-drain/Wait, no external process
// needed beyond cmd.exe (already used by SapiHandleExec above).
TEST(SapiHandleExecStartWait) {
  W2gResult rc;
  std::string req = std::string("cmd.exe") + '\x1f' + "/c" + '\x1f' + "echo" + '\x1f' + "start-wait-works";
  std::string start = Call("os.exec.start", req, &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  std::string h = HandleOf(start);
  EXPECT(!h.empty());

  std::string out;
  for (int i = 0; i < 20; ++i) {
    std::string chunk = Call("os.exec.stdout.read", h + "\x1f" + "4096", &rc);
    EXPECT_EQ(rc, W2G_RESULT_OK);
    if (chunk.empty()) break;
    out += chunk;
  }
  EXPECT(out.find("start-wait-works") != std::string::npos);

  std::string wait = Call("os.exec.wait", h, &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  EXPECT(wait == "exit=0");
}

TEST(SapiHandleUserLookupCurrent) {
  W2gResult rc;
  std::string me = Call("os.user", "", &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  auto p1 = me.find('\x1f');
  EXPECT(p1 != std::string::npos);
  std::string username = me.substr(p1 + 1, me.find('\x1f', p1 + 1) - p1 - 1);

  std::string looked_up = Call("os.user", "lookup " + username, &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  EXPECT(looked_up.find(username) != std::string::npos);
}

TEST(SapiHandleSyscallChdir) {
  // All tests run sequentially in one process (see tests/test_main.cc),
  // so a real SetCurrentDirectoryW here is real, permanent, process-wide
  // state -- save and restore the exact prior directory, don't guess.
  wchar_t prev[MAX_PATH];
  GetCurrentDirectoryW(MAX_PATH, prev);

  W2gResult rc;
  std::string r = Call("syscall", "chdir C:\\Windows", &rc);
  EXPECT_EQ(rc, W2G_RESULT_OK);
  EXPECT(r == "ok");

  SetCurrentDirectoryW(prev);
}

TEST(SapiHandleUnknown) {
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[8];
  uint32_t n = sizeof(buf);
  EXPECT_EQ(W2gSapiHandle("nope", nullptr, 0, topic, sizeof(topic), buf, &n),
            W2G_RESULT_NOT_FOUND);
}
