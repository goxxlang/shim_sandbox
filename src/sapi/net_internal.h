#ifndef W2G_SAPI_NET_INTERNAL_H_
#define W2G_SAPI_NET_INTERNAL_H_

// Small helpers shared between real_win.cc and tls_win.cc -- mainly so
// there is exactly one real DNS-resolve-and-connect implementation
// (ConnectReal) rather than two to keep in sync.
#include "handles.h"
#include "real.h"

#include <string>

namespace w2g {
namespace real {
namespace internal {

void EnsureWinsock();
std::string WinErr(int code);
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const wchar_t* w, int wlen = -1);

// "host:port" / ":port" (host omitted means wildcard) / "[::1]:port".
bool SplitHostPort(const std::string& hostport, std::string* host, std::string* port);
// Splits on the first occurrence of sep only; *b is unsplit remainder.
bool SplitOne(const std::string& s, char sep, std::string* a, std::string* b);
bool ParseHandle(const std::string& s, Handle* out);
std::string FormatSockAddr(const sockaddr_storage& ss);
int SockType(const std::string& network);
int Family(const std::string& network);

// Resolves address (real DNS) and connects. On success leaves the
// connected socket in *out (caller owns it) and the reply payload is
// "ok local=.. remote=..". On failure *out is left untouched (caller
// must init it to INVALID_SOCKET first) and the reply describes why.
Reply ConnectReal(const std::string& network, const std::string& address, SOCKET* out);

}  // namespace internal
}  // namespace real
}  // namespace w2g

#endif  // W2G_SAPI_NET_INTERNAL_H_
