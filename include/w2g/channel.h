#ifndef W2G_CHANNEL_H_
#define W2G_CHANNEL_H_

#include "w2g/sapi.h"

#include <cstdint>

namespace w2g {

// RPC to the extra G++ library. Default is in-process passthrough so
// Windows, host G++, and wasm32-wasip1 all work. Same call shape SAPI
// uses between host and sandboxee.
class RpcChannel {
 public:
  virtual ~RpcChannel() = default;
  virtual W2gResult Handle(const char* topic, const uint8_t* req, uint32_t req_len,
                           char* reply_topic, uint32_t reply_topic_cap,
                           uint8_t* reply, uint32_t* reply_len) = 0;
};

class PassthroughChannel : public RpcChannel {
 public:
  W2gResult Handle(const char* topic, const uint8_t* req, uint32_t req_len,
                   char* reply_topic, uint32_t reply_topic_cap, uint8_t* reply,
                   uint32_t* reply_len) override;
};

RpcChannel& DefaultChannel();
void SetDefaultChannel(RpcChannel* channel);

}  // namespace w2g

#endif  // W2G_CHANNEL_H_
