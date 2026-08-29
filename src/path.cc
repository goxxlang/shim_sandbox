#include "w2g/path.h"

#include <cctype>
#include <vector>

namespace w2g {

std::string CanonicalPath(std::string_view in) {
  if (in.empty() || in.size() > 4096) return {};
  for (unsigned char c : in) {
    if (c < 0x20 || c == 0x7f) return {};
  }
  std::string s(in);
  for (char& c : s) {
    if (c == '\\') c = '/';
  }
  std::string prefix;
  if (s.size() >= 2 && s[1] == ':' && std::isalpha(static_cast<unsigned char>(s[0]))) {
    prefix.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[0]))));
    prefix += ':';
    s = s.substr(2);
    if (s.empty() || s[0] != '/') s.insert(s.begin(), '/');
  }
  const bool abs = !s.empty() && s[0] == '/';
  std::vector<std::string> st;
  size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '/') {
      ++i;
      continue;
    }
    size_t j = i;
    while (j < s.size() && s[j] != '/') ++j;
    std::string part = s.substr(i, j - i);
    i = j;
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      if (st.empty()) {
        if (abs || !prefix.empty()) continue;
        return {};
      }
      st.pop_back();
      continue;
    }
    st.push_back(std::move(part));
  }
  std::string out = prefix;
  if (abs || !prefix.empty()) out += '/';
  for (size_t k = 0; k < st.size(); ++k) {
    if (k) out += '/';
    out += st[k];
  }
  if (st.empty() && (abs || !prefix.empty()) && (out.empty() || out.back() != '/')) {
    out += '/';
  }
  return out;
}

bool PrefixBound(std::string_view prefix, std::string_view val) {
  if (prefix.empty()) return false;
  if (val.size() < prefix.size()) return false;
  if (val.compare(0, prefix.size(), prefix) != 0) return false;
  if (val.size() == prefix.size()) return true;
  if (prefix.back() == '/') return true;
  return val[prefix.size()] == '/';
}

}  // namespace w2g
