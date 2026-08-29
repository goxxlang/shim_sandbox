#ifndef W2G_MSG_H_
#define W2G_MSG_H_

#include "w2g/pipe.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace w2g {

inline constexpr uint32_t kMaxPayloadBytes = 16u << 20;
inline constexpr size_t kMaxLayerName = 128;
inline constexpr size_t kMaxTopic = 256;
inline constexpr size_t kMaxLayers = 64;

// One Pipe write. Topic is the pub/sub key; payload is opaque bytes.
// id matches a Call() to its reply (0 if the publisher did not set one).
struct Msg {
  std::string from;
  std::string topic;
  uint32_t id = 0;
  wasigo::Slice<uint8_t> payload;
};

inline wasigo::Slice<uint8_t> ToSlice(std::string_view s) {
  auto out = wasigo::make_slice<uint8_t>(static_cast<int64_t>(s.size()),
                                        static_cast<int64_t>(s.size()));
  for (size_t i = 0; i < s.size(); ++i) {
    out[static_cast<int64_t>(i)] = static_cast<uint8_t>(s[i]);
  }
  return out;
}

inline wasigo::Slice<uint8_t> ToSlice(const std::vector<uint8_t>& s) {
  auto out = wasigo::make_slice<uint8_t>(static_cast<int64_t>(s.size()),
                                        static_cast<int64_t>(s.size()));
  for (size_t i = 0; i < s.size(); ++i) {
    out[static_cast<int64_t>(i)] = s[i];
  }
  return out;
}

inline std::string ToString(const wasigo::Slice<uint8_t>& s) {
  std::string o;
  o.resize(static_cast<size_t>(s.len()));
  for (int64_t i = 0; i < s.len(); ++i) {
    o[static_cast<size_t>(i)] = static_cast<char>(s[i]);
  }
  return o;
}

inline void PutU16(std::vector<uint8_t>& o, uint16_t v) {
  o.push_back(static_cast<uint8_t>(v));
  o.push_back(static_cast<uint8_t>(v >> 8));
}

inline void PutU32(std::vector<uint8_t>& o, uint32_t v) {
  o.push_back(static_cast<uint8_t>(v));
  o.push_back(static_cast<uint8_t>(v >> 8));
  o.push_back(static_cast<uint8_t>(v >> 16));
  o.push_back(static_cast<uint8_t>(v >> 24));
}

inline bool GetU16(const wasigo::Slice<uint8_t>& s, int64_t* i, uint16_t* v) {
  if (*i + 2 > s.len()) return false;
  *v = static_cast<uint16_t>(s[*i] | (static_cast<uint16_t>(s[*i + 1]) << 8));
  *i += 2;
  return true;
}

inline bool GetU32(const wasigo::Slice<uint8_t>& s, int64_t* i, uint32_t* v) {
  if (*i + 4 > s.len()) return false;
  *v = static_cast<uint32_t>(s[*i]) |
       (static_cast<uint32_t>(s[*i + 1]) << 8) |
       (static_cast<uint32_t>(s[*i + 2]) << 16) |
       (static_cast<uint32_t>(s[*i + 3]) << 24);
  *i += 4;
  return true;
}

inline bool GetBytes(const wasigo::Slice<uint8_t>& s, int64_t* i, uint32_t n,
                     std::string* out) {
  if (*i + static_cast<int64_t>(n) > s.len()) return false;
  out->assign(static_cast<size_t>(n), '\0');
  for (uint32_t k = 0; k < n; ++k) {
    (*out)[k] = static_cast<char>(s[*i + static_cast<int64_t>(k)]);
  }
  *i += static_cast<int64_t>(n);
  return true;
}

inline bool GetSlice(const wasigo::Slice<uint8_t>& s, int64_t* i, uint32_t n,
                     wasigo::Slice<uint8_t>* out) {
  if (*i + static_cast<int64_t>(n) > s.len()) return false;
  *out = s.slice(*i, *i + static_cast<int64_t>(n));
  *i += static_cast<int64_t>(n);
  return true;
}

// ver=1 | from | topic | id u32 | payload
inline std::vector<uint8_t> Encode(const Msg& m) {
  std::vector<uint8_t> o;
  o.push_back(1);
  if (m.from.size() > 0xffff || m.topic.size() > 0xffff) return {};
  if (m.payload.len() < 0 || static_cast<uint64_t>(m.payload.len()) > kMaxPayloadBytes) {
    return {};
  }
  PutU16(o, static_cast<uint16_t>(m.from.size()));
  o.insert(o.end(), m.from.begin(), m.from.end());
  PutU16(o, static_cast<uint16_t>(m.topic.size()));
  o.insert(o.end(), m.topic.begin(), m.topic.end());
  PutU32(o, m.id);
  PutU32(o, static_cast<uint32_t>(m.payload.len()));
  for (int64_t i = 0; i < m.payload.len(); ++i) {
    o.push_back(m.payload[i]);
  }
  return o;
}

inline bool Decode(const wasigo::Slice<uint8_t>& s, Msg* out) {
  if (!out || s.len() < 1 || s[0] != 1) return false;
  int64_t i = 1;
  uint16_t from_len = 0;
  uint16_t topic_len = 0;
  uint32_t payload_len = 0;
  if (!GetU16(s, &i, &from_len)) return false;
  if (!GetBytes(s, &i, from_len, &out->from)) return false;
  if (!GetU16(s, &i, &topic_len)) return false;
  if (!GetBytes(s, &i, topic_len, &out->topic)) return false;
  if (!GetU32(s, &i, &out->id)) return false;
  if (!GetU32(s, &i, &payload_len)) return false;
  if (payload_len > kMaxPayloadBytes) return false;
  if (!GetSlice(s, &i, payload_len, &out->payload)) return false;
  return i == s.len();
}

}  // namespace w2g

#endif  // W2G_MSG_H_
