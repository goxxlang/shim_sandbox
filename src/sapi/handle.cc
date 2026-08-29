#include "w2g/sapi.h"

#include <cstring>

namespace {

const char* ReplyTopic(const char* topic) {
  if (!topic) return nullptr;
  if (std::strcmp(topic, "net.dial") == 0) return "net.dial.reply";
  if (std::strcmp(topic, "net.listen") == 0) return "net.listen.reply";
  if (std::strcmp(topic, "net.tcp.bind") == 0) return "net.tcp.bind.reply";
  if (std::strcmp(topic, "net.udp.bind") == 0) return "net.udp.bind.reply";
  return nullptr;
}

constexpr const char kNotSupported[] =
    "net: not supported on wasm32-wasip1 (WASI preview 1 has no socket syscalls)";

}  // namespace

extern "C" W2gResult W2gSapiHandle(const char* topic, const uint8_t* req,
                                   uint32_t req_len, char* reply_topic,
                                   uint32_t reply_topic_cap, uint8_t* reply,
                                   uint32_t* reply_len) {
  (void)req;
  (void)req_len;
  const char* dest = ReplyTopic(topic);
  if (!dest) return W2G_RESULT_NOT_FOUND;
  if (!reply_topic || !reply || !reply_len) return W2G_RESULT_INVALID_ARGUMENT;

  const uint32_t tlen = static_cast<uint32_t>(std::strlen(dest));
  if (reply_topic_cap < tlen + 1) return W2G_RESULT_INVALID_ARGUMENT;
  std::memcpy(reply_topic, dest, tlen + 1);

  const uint32_t plen = static_cast<uint32_t>(sizeof(kNotSupported) - 1);
  if (*reply_len < plen) return W2G_RESULT_INVALID_ARGUMENT;
  std::memcpy(reply, kNotSupported, plen);
  *reply_len = plen;
  return W2G_RESULT_UNIMPLEMENTED;
}
