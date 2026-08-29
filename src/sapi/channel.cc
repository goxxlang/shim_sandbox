#include "w2g/channel.h"

namespace w2g {

W2gResult PassthroughChannel::Handle(const char* topic, const uint8_t* req,
                                     uint32_t req_len, char* reply_topic,
                                     uint32_t reply_topic_cap, uint8_t* reply,
                                     uint32_t* reply_len) {
  return W2gSapiHandle(topic, req, req_len, reply_topic, reply_topic_cap, reply,
                       reply_len);
}

namespace {

PassthroughChannel g_passthrough;
RpcChannel* g_channel = &g_passthrough;

}  // namespace

RpcChannel& DefaultChannel() { return *g_channel; }

void SetDefaultChannel(RpcChannel* channel) {
  g_channel = channel ? channel : &g_passthrough;
}

}  // namespace w2g
