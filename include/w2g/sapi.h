#ifndef W2G_SAPI_H_
#define W2G_SAPI_H_

// Extra G++ library surface in the shape google/sandboxed-api wraps:
// plain C functions, no C++. Host code talks to this through an RPC
// channel (passthrough on every OS). We do not take sandboxed-api as a
// dependency — its sandbox2 backend is Linux-only.

#include "w2g/c/types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define W2G_SAPI_TOPIC_MAX 64u
#define W2G_SAPI_PAYLOAD_MAX 65536u

// One extra-layer call. Known stub topics fill reply_topic + reply and
// return W2G_RESULT_UNIMPLEMENTED. Unknown topics return NOT_FOUND.
W2gResult W2gSapiHandle(const char* topic, const uint8_t* req, uint32_t req_len,
                        char* reply_topic, uint32_t reply_topic_cap,
                        uint8_t* reply, uint32_t* reply_len);

#ifdef __cplusplus
}
#endif

#endif  // W2G_SAPI_H_
