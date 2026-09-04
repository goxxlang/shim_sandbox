#include "w2g/sapi.h"

#include "w2g/system_policy.h"

#include <cstring>
#include <string>

#if W2G_ABAC_SYSTEM
#include "real.h"
#endif

namespace {

const char* ReplyTopic(const char* topic) {
  if (!topic) return nullptr;
  if (std::strcmp(topic, "net.dial") == 0) return "net.dial.reply";
  if (std::strcmp(topic, "net.listen") == 0) return "net.listen.reply";
  if (std::strcmp(topic, "net.tcp.bind") == 0) return "net.tcp.bind.reply";
  if (std::strcmp(topic, "net.udp.bind") == 0) return "net.udp.bind.reply";
  if (std::strcmp(topic, "net.accept") == 0) return "net.accept.reply";
  if (std::strcmp(topic, "net.io.read") == 0) return "net.io.read.reply";
  if (std::strcmp(topic, "net.io.write") == 0) return "net.io.write.reply";
  if (std::strcmp(topic, "net.io.readfrom") == 0) return "net.io.readfrom.reply";
  if (std::strcmp(topic, "net.io.writeto") == 0) return "net.io.writeto.reply";
  if (std::strcmp(topic, "net.io.close") == 0) return "net.io.close.reply";
  if (std::strcmp(topic, "os.exec") == 0) return "os.exec.reply";
  if (std::strcmp(topic, "os.exec.start") == 0) return "os.exec.start.reply";
  if (std::strcmp(topic, "os.exec.wait") == 0) return "os.exec.wait.reply";
  if (std::strcmp(topic, "os.exec.stdout.read") == 0) return "os.exec.stdout.read.reply";
  if (std::strcmp(topic, "os.exec.stdin.write") == 0) return "os.exec.stdin.write.reply";
  if (std::strcmp(topic, "os.exec.stdin.close") == 0) return "os.exec.stdin.close.reply";
  if (std::strcmp(topic, "os.user") == 0) return "os.user.reply";
  if (std::strcmp(topic, "syscall") == 0) return "syscall.reply";
  if (std::strcmp(topic, "tls.dial") == 0) return "tls.dial.reply";
  if (std::strcmp(topic, "tls.io.read") == 0) return "tls.io.read.reply";
  if (std::strcmp(topic, "tls.io.write") == 0) return "tls.io.write.reply";
  if (std::strcmp(topic, "tls.io.close") == 0) return "tls.io.close.reply";
  return nullptr;
}

constexpr const char kNotSupported[] =
    "net: not supported on wasm32-wasip1 (WASI preview 1 has no socket syscalls)";

bool CopyOut(const std::string& payload, char* reply_topic, const char* dest,
            uint32_t reply_topic_cap, uint8_t* reply, uint32_t* reply_len) {
  const uint32_t tlen = static_cast<uint32_t>(std::strlen(dest));
  if (reply_topic_cap < tlen + 1) return false;
  std::memcpy(reply_topic, dest, tlen + 1);

  uint32_t plen = static_cast<uint32_t>(payload.size());
  if (plen > *reply_len) plen = *reply_len;  // truncate rather than fail; reply is best-effort text
  std::memcpy(reply, payload.data(), plen);
  *reply_len = plen;
  return true;
}

#if W2G_ABAC_SYSTEM
// "<a> <b>" -> a, b (net.dial/net.listen's "<network> <address>" shape).
void SplitSpace(const std::string& s, std::string* a, std::string* b) {
  auto sp = s.find(' ');
  *a = sp == std::string::npos ? s : s.substr(0, sp);
  *b = sp == std::string::npos ? "" : s.substr(sp + 1);
}

w2g::real::Reply Dispatch(const char* topic, const std::string& payload) {
  using namespace w2g::real;  // NOLINT
  std::string a, b;
  if (std::strcmp(topic, "net.dial") == 0) {
    SplitSpace(payload, &a, &b);
    return Dial(a, b);
  }
  if (std::strcmp(topic, "net.listen") == 0) {
    SplitSpace(payload, &a, &b);
    return ListenProbe(a, b);
  }
  if (std::strcmp(topic, "net.tcp.bind") == 0) return TcpBindProbe(payload);
  if (std::strcmp(topic, "net.udp.bind") == 0) return UdpBindProbe(payload);
  if (std::strcmp(topic, "net.accept") == 0) return Accept(payload);
  if (std::strcmp(topic, "net.io.read") == 0) return IoRead(payload);
  if (std::strcmp(topic, "net.io.write") == 0) return IoWrite(payload);
  if (std::strcmp(topic, "net.io.readfrom") == 0) return IoReadFrom(payload);
  if (std::strcmp(topic, "net.io.writeto") == 0) return IoWriteTo(payload);
  if (std::strcmp(topic, "net.io.close") == 0) return IoClose(payload);
  if (std::strcmp(topic, "os.exec") == 0) return Exec(payload);
  if (std::strcmp(topic, "os.exec.start") == 0) return ExecStart(payload);
  if (std::strcmp(topic, "os.exec.wait") == 0) return ExecWait(payload);
  if (std::strcmp(topic, "os.exec.stdout.read") == 0) return ExecStdoutRead(payload);
  if (std::strcmp(topic, "os.exec.stdin.write") == 0) return ExecStdinWrite(payload);
  if (std::strcmp(topic, "os.exec.stdin.close") == 0) return ExecStdinClose(payload);
  if (std::strcmp(topic, "os.user") == 0) return User(payload);
  if (std::strcmp(topic, "syscall") == 0) return Syscall(payload);
  if (std::strcmp(topic, "tls.dial") == 0) return TlsDial(payload);
  if (std::strcmp(topic, "tls.io.read") == 0) return TlsIoRead(payload);
  if (std::strcmp(topic, "tls.io.write") == 0) return TlsIoWrite(payload);
  return TlsIoClose(payload);  // tls.io.close
}
#endif

}  // namespace

extern "C" W2gResult W2gSapiHandle(const char* topic, const uint8_t* req,
                                   uint32_t req_len, char* reply_topic,
                                   uint32_t reply_topic_cap, uint8_t* reply,
                                   uint32_t* reply_len) {
  const char* dest = ReplyTopic(topic);
  if (!dest) return W2G_RESULT_NOT_FOUND;
  if (!reply_topic || !reply || !reply_len) return W2G_RESULT_INVALID_ARGUMENT;

  std::string payload;
  if (req && req_len) payload.assign(reinterpret_cast<const char*>(req), req_len);

#if W2G_ABAC_SYSTEM
  w2g::real::Reply r = Dispatch(topic, payload);
  if (!CopyOut(r.payload, reply_topic, dest, reply_topic_cap, reply, reply_len)) {
    return W2G_RESULT_INVALID_ARGUMENT;
  }
  return r.code;
#else
  (void)payload;
  if (!CopyOut(kNotSupported, reply_topic, dest, reply_topic_cap, reply, reply_len)) {
    return W2G_RESULT_INVALID_ARGUMENT;
  }
  return W2G_RESULT_UNIMPLEMENTED;
#endif
}
