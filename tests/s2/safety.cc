#include "test.h"

#include "s2/os.h"
#include "w2g/s2/buffer.h"
#include "w2g/s2/comms.h"

#include <cstdint>
#include <thread>
#include <vector>

TEST(S2CommsRejectsOversize) {
  auto pair = w2g::s2::MakeCommsPair();
  pair.parent.set_max_msg_bytes(32);
  uint8_t hdr[8] = {5, 0, 0, 0, 0xff, 0xff, 0x00, 0x00};  // tag=bytes, len=65535
  EXPECT(w2g::s2::os::Write(pair.child_w.get(), hdr, 8));
  std::vector<uint8_t> got;
  EXPECT(!pair.parent.RecvTLV(nullptr, &got));
  EXPECT(got.size() <= 32);
}

TEST(S2BufferBounds) {
  auto buf = w2g::s2::Buffer::CreateWithSize(8);
  EXPECT(buf != nullptr);
  EXPECT(!w2g::s2::Buffer::CreateWithSize(0));
  EXPECT(!w2g::s2::Buffer::CreateWithSize(w2g::s2::kMaxBufferBytes + 1));
  uint8_t x = 7;
  EXPECT(buf->CopyTo(0, &x, 1));
  uint8_t y = 0;
  EXPECT(buf->Get(0, &y));
  EXPECT_EQ(y, 7);
  EXPECT(!buf->CopyTo(8, &x, 1));
  EXPECT(!buf->CopyFrom(7, &y, 2));
  EXPECT(!buf->Get(8, &y));
}

TEST(S2SendNullRejected) {
  auto pair = w2g::s2::MakeCommsPair();
  EXPECT(!pair.parent.SendTLV(w2g::s2::Comms::kTagBytes, nullptr, 4));
}
