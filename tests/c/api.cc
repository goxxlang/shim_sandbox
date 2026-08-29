#include "test.h"

#include "w2g/c/system.h"

TEST(CBusAttach) {
  W2gBus* bus = W2gBusCreate();
  EXPECT(bus != nullptr);
  EXPECT_EQ(W2gAttach(bus, "wasi", W2G_SIDE_WASI), W2G_RESULT_OK);
  EXPECT_EQ(W2gAttach(bus, "gxx", W2G_SIDE_GXX), W2G_RESULT_OK);
  EXPECT_EQ(W2gAttach(bus, "wasi", W2G_SIDE_WASI), W2G_RESULT_ALREADY_EXISTS);
  EXPECT_EQ(W2gSubscribe(bus, "gxx", "net.dial"), W2G_RESULT_OK);
  EXPECT_EQ(W2gSubscribe(bus, "missing", "x"), W2G_RESULT_NOT_FOUND);
  EXPECT_EQ(W2gCloseLayer(bus, "gxx"), W2G_RESULT_OK);
  W2gBusDestroy(bus);
}
