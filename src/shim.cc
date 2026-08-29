#include "w2g/shim.h"

#include "w2g/s2/comms.h"

#include <cstdio>
#include <vector>

namespace w2g {

wasigo::Error Shim::Denied(std::string_view action, const std::string& resource) const {
  return wasigo::errors_new("abac deny " + std::string(action) + " " + resource);
}

wasigo::OsOpenResult Shim::OpenHost(const std::string& name) {
  return wasigo::os_open(name);
}

wasigo::OsOpenResult Shim::CreateHost(const std::string& name) {
  return wasigo::os_create(name);
}

wasigo::OsReadFileResult Shim::ReadFileHost(const std::string& name) {
  FILE* raw = std::fopen(name.c_str(), "rb");
  if (!raw) {
    return {wasigo::Slice<uint8_t>{}, wasigo::errors_new("open " + name + ": no such file or directory")};
  }
  std::vector<uint8_t> data;
  uint8_t buf[4096];
  for (;;) {
    size_t n = std::fread(buf, 1, sizeof(buf), raw);
    if (n > 0) {
      if (data.size() + n > s2::kMaxMsgBytes) {
        std::fclose(raw);
        return {wasigo::Slice<uint8_t>{}, wasigo::errors_new("read too large")};
      }
      data.insert(data.end(), buf, buf + n);
    }
    if (n < sizeof(buf)) break;
  }
  std::fclose(raw);
  wasigo::Slice<uint8_t> out;
  out.buf = std::make_shared<std::vector<uint8_t>>(std::move(data));
  out.len_ = out.buf->size();
  return {out, wasigo::Error()};
}

wasigo::Error Shim::WriteFileHost(const std::string& name, wasigo::Slice<uint8_t> data,
                                  int64_t perm) {
  if (data.size() > s2::kMaxMsgBytes) return wasigo::errors_new("write too large");
  return wasigo::os_write_file(name, data, perm);
}

std::string Shim::GetenvHost(const std::string& key) {
  return wasigo::os_getenv(key);
}

}  // namespace w2g
