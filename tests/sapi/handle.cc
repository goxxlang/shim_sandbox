#include "test.h"

#include "w2g/sapi.h"

#include <cstring>
#include <string>

TEST(SapiHandleDial) {
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[256];
  uint32_t n = sizeof(buf);
  W2gResult rc = W2gSapiHandle("net.dial", nullptr, 0, topic, sizeof(topic), buf, &n);
  EXPECT_EQ(rc, W2G_RESULT_UNIMPLEMENTED);
  EXPECT(std::strcmp(topic, "net.dial.reply") == 0);
  EXPECT(std::string(reinterpret_cast<char*>(buf), n).find("not supported") !=
         std::string::npos);
}

TEST(SapiHandleUnknown) {
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  uint8_t buf[8];
  uint32_t n = sizeof(buf);
  EXPECT_EQ(W2gSapiHandle("nope", nullptr, 0, topic, sizeof(topic), buf, &n),
            W2G_RESULT_NOT_FOUND);
}
