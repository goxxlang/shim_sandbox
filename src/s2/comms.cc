#include "w2g/s2/comms.h"

#include "os.h"

namespace w2g {
namespace s2 {

void Comms::Terminate() {
  r_.reset();
  w_.reset();
}

bool Comms::WriteAll(const void* p, uint32_t n) {
  if (!w_.valid()) return false;
  if (n == 0) return true;
  if (!p) return false;
  return os::Write(w_.get(), p, n);
}

bool Comms::ReadAll(void* p, uint32_t n) {
  if (!r_.valid()) return false;
  if (n == 0) return true;
  if (!p) return false;
  return os::Read(r_.get(), p, n);
}

static void PutU32(uint8_t* o, uint32_t v) {
  o[0] = static_cast<uint8_t>(v);
  o[1] = static_cast<uint8_t>(v >> 8);
  o[2] = static_cast<uint8_t>(v >> 16);
  o[3] = static_cast<uint8_t>(v >> 24);
}

static uint32_t GetU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool Comms::SendTLV(uint32_t tag, const void* data, uint32_t len) {
  if (len > max_msg_) return false;
  if (len > 0 && !data) return false;
  uint8_t hdr[8];
  PutU32(hdr, tag);
  PutU32(hdr + 4, len);
  if (!WriteAll(hdr, 8)) return false;
  return len == 0 || WriteAll(data, len);
}

bool Comms::RecvTLV(uint32_t* tag, std::vector<uint8_t>* value) {
  uint8_t hdr[8];
  if (!ReadAll(hdr, 8)) return false;
  uint32_t t = GetU32(hdr);
  uint32_t n = GetU32(hdr + 4);
  if (tag) *tag = t;
  if (!value) return false;
  if (n > max_msg_) {
    Terminate();
    return false;
  }
  try {
    value->assign(n, 0);
  } catch (...) {
    Terminate();
    return false;
  }
  return n == 0 || ReadAll(value->data(), n);
}

bool Comms::SendU32(uint32_t v) {
  uint8_t b[4];
  PutU32(b, v);
  return SendTLV(kTagU32, b, 4);
}

bool Comms::RecvU32(uint32_t* v) {
  uint32_t tag = 0;
  std::vector<uint8_t> b;
  if (!RecvTLV(&tag, &b) || tag != kTagU32 || b.size() != 4) return false;
  if (v) *v = GetU32(b.data());
  return true;
}

bool Comms::SendU64(uint64_t v) {
  uint8_t b[8];
  PutU32(b, static_cast<uint32_t>(v));
  PutU32(b + 4, static_cast<uint32_t>(v >> 32));
  return SendTLV(kTagU64, b, 8);
}

bool Comms::RecvU64(uint64_t* v) {
  uint32_t tag = 0;
  std::vector<uint8_t> b;
  if (!RecvTLV(&tag, &b) || tag != kTagU64 || b.size() != 8) return false;
  if (v) {
    *v = static_cast<uint64_t>(GetU32(b.data())) |
         (static_cast<uint64_t>(GetU32(b.data() + 4)) << 32);
  }
  return true;
}

bool Comms::SendBool(bool v) {
  uint8_t x = v ? 1 : 0;
  return SendTLV(kTagBool, &x, 1);
}

bool Comms::RecvBool(bool* v) {
  uint32_t tag = 0;
  std::vector<uint8_t> b;
  if (!RecvTLV(&tag, &b) || tag != kTagBool || b.size() != 1) return false;
  if (v) *v = b[0] != 0;
  return true;
}

bool Comms::SendString(const std::string& v) {
  if (v.size() > max_msg_) return false;
  return SendTLV(kTagString, v.data(), static_cast<uint32_t>(v.size()));
}

bool Comms::RecvString(std::string* v) {
  uint32_t tag = 0;
  std::vector<uint8_t> b;
  if (!RecvTLV(&tag, &b) || tag != kTagString) return false;
  if (v) {
    if (b.empty()) {
      v->clear();
    } else {
      v->assign(reinterpret_cast<char*>(b.data()), b.size());
    }
  }
  return true;
}

bool Comms::SendBytes(const uint8_t* p, uint32_t n) {
  return SendTLV(kTagBytes, p, n);
}

bool Comms::RecvBytes(std::vector<uint8_t>* v) {
  uint32_t tag = 0;
  return RecvTLV(&tag, v) && tag == kTagBytes;
}

CommsPair MakeCommsPair() {
  auto p = os::MakeDuplex();
  CommsPair o;
  o.parent = Comms(std::move(p.parent_r), std::move(p.parent_w));
  o.child_r = std::move(p.child_r);
  o.child_w = std::move(p.child_w);
  return o;
}

}  // namespace s2
}  // namespace w2g
