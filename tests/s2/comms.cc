#include "test.h"

#include "w2g/s2/buffer.h"
#include "w2g/s2/comms.h"

#include <thread>

TEST(S2CommsPing) {
  auto pair = w2g::s2::MakeCommsPair();
  uint32_t got = 0;
  std::thread t([&] {
    pair.parent.RecvU32(&got);
  });
  w2g::s2::Comms child(std::move(pair.child_r), std::move(pair.child_w));
  EXPECT(child.SendU32(7));
  t.join();
  EXPECT_EQ(got, 7u);
}

TEST(S2CommsString) {
  auto pair = w2g::s2::MakeCommsPair();
  std::string got;
  std::thread t([&] { pair.parent.RecvString(&got); });
  w2g::s2::Comms child(std::move(pair.child_r), std::move(pair.child_w));
  EXPECT(child.SendString("hello"));
  t.join();
  EXPECT(got == "hello");
}

TEST(S2Buffer) {
  auto buf = w2g::s2::Buffer::CreateWithSize(4096);
  EXPECT(buf != nullptr);
  EXPECT(buf->size() == 4096);
  EXPECT(buf->Set(0, 42));
  uint8_t v = 0;
  EXPECT(buf->Get(0, &v));
  EXPECT_EQ(v, 42);
}
