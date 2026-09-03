#include "net_internal.h"

#include <cstring>
#include <mutex>
#include <sstream>

namespace w2g {
namespace real {
namespace internal {

void EnsureWinsock() {
  static std::once_flag once;
  std::call_once(once, [] {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
  });
}

std::string WinErr(int code) {
  char buf[256] = {};
  DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                           static_cast<DWORD>(code), 0, buf, sizeof(buf), nullptr);
  std::string s(buf, n);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  std::ostringstream o;
  o << s << " (" << code << ")";
  return o.str();
}

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string WideToUtf8(const wchar_t* w, int wlen) {
  if (!w || wlen == 0) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, wlen, s.data(), n, nullptr, nullptr);
  if (wlen < 0 && !s.empty() && s.back() == '\0') s.pop_back();
  return s;
}

bool SplitHostPort(const std::string& hostport, std::string* host, std::string* port) {
  if (hostport.empty()) return false;
  if (hostport[0] == '[') {
    auto end = hostport.find("]:");
    if (end == std::string::npos) return false;
    *host = hostport.substr(1, end - 1);
    *port = hostport.substr(end + 2);
    return true;
  }
  auto i = hostport.rfind(':');
  if (i == std::string::npos) return false;
  *host = hostport.substr(0, i);
  *port = hostport.substr(i + 1);
  return true;
}

bool SplitOne(const std::string& s, char sep, std::string* a, std::string* b) {
  auto i = s.find(sep);
  if (i == std::string::npos) {
    *a = s;
    *b = "";
    return false;
  }
  *a = s.substr(0, i);
  *b = s.substr(i + 1);
  return true;
}

bool ParseHandle(const std::string& s, Handle* out) {
  if (s.empty()) return false;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
  }
  *out = std::strtoull(s.c_str(), nullptr, 10);
  return true;
}

std::string FormatSockAddr(const sockaddr_storage& ss) {
  char host[NI_MAXHOST] = {};
  char serv[NI_MAXSERV] = {};
  socklen_t len = ss.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
  if (getnameinfo(reinterpret_cast<const sockaddr*>(&ss), len, host, sizeof(host), serv,
                  sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
    return "?";
  }
  std::ostringstream o;
  o << host << ":" << serv;
  return o.str();
}

int SockType(const std::string& network) {
  return (network == "udp" || network == "udp4" || network == "udp6") ? SOCK_DGRAM : SOCK_STREAM;
}

int Family(const std::string& network) {
  if (network == "tcp6" || network == "udp6") return AF_INET6;
  if (network == "tcp4" || network == "udp4") return AF_INET;
  return AF_UNSPEC;
}

Reply ConnectReal(const std::string& network, const std::string& address, SOCKET* out) {
  EnsureWinsock();
  std::string host, port;
  if (!SplitHostPort(address, &host, &port)) {
    return {W2G_RESULT_INVALID_ARGUMENT, "error: missing port in address " + address};
  }
  addrinfo hints{};
  hints.ai_family = Family(network);
  hints.ai_socktype = SockType(network);
  addrinfo* res = nullptr;
  int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
  if (rc != 0) {
    return {W2G_RESULT_OK, "error: resolve " + address + ": " + gai_strerrorA(rc)};
  }
  SOCKET s = INVALID_SOCKET;
  int last_err = 0;
  for (addrinfo* p = res; p; p = p->ai_next) {
    s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s == INVALID_SOCKET) {
      last_err = WSAGetLastError();
      continue;
    }
    if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
      sockaddr_storage local{};
      int llen = sizeof(local);
      getsockname(s, reinterpret_cast<sockaddr*>(&local), &llen);
      sockaddr_storage remote{};
      std::memcpy(&remote, p->ai_addr, p->ai_addrlen);
      freeaddrinfo(res);
      *out = s;
      return {W2G_RESULT_OK,
              "ok local=" + FormatSockAddr(local) + " remote=" + FormatSockAddr(remote)};
    }
    last_err = WSAGetLastError();
    closesocket(s);
    s = INVALID_SOCKET;
  }
  freeaddrinfo(res);
  return {W2G_RESULT_OK, "error: connect " + address + ": " + WinErr(last_err)};
}

}  // namespace internal
}  // namespace real
}  // namespace w2g
