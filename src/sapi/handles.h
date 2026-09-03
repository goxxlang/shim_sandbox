#ifndef W2G_SAPI_HANDLES_H_
#define W2G_SAPI_HANDLES_H_

// Windows-only (real_win.cc / tls_win.cc's own home). Live resources a
// gocvm topic handed back a handle for: an open socket, a running
// process, an established TLS session. No locking -- wasigo's scheduler
// is cooperative single-threaded (same assumption every other global in
// this codebase already makes), and every handler that touches these is
// itself one synchronous gocvm.Call.

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <sspi.h>
#include <schannel.h>

#include <cstdint>
#include <vector>

namespace w2g {
namespace real {

using Handle = uint64_t;

struct SockEntry {
  SOCKET s = INVALID_SOCKET;
  bool udp = false;
};

struct ProcEntry {
  HANDLE process = nullptr;
  HANDLE stdout_read = nullptr;
  HANDLE stdin_write = nullptr;
};

struct TlsEntry {
  SOCKET s = INVALID_SOCKET;
  CredHandle cred{};
  CtxtHandle ctx{};
  SecPkgContext_StreamSizes sizes{};
  // Plaintext DecryptMessage handed back beyond what the caller asked
  // for in one tls.io.read, plus any raw bytes read off the socket that
  // turned out to be more than one TLS record -- same role net.Conn's
  // own `leftover` plays in Go++, just kept on the C++ side since the
  // TLS record framing has to be parsed here anyway.
  std::vector<uint8_t> plaintext;
  std::vector<uint8_t> raw_extra;
};

Handle AllocSocket(SOCKET s, bool udp);
Handle AllocProcess(HANDLE process, HANDLE stdout_read, HANDLE stdin_write);
Handle AllocTls(SOCKET s, CredHandle cred, CtxtHandle ctx, SecPkgContext_StreamSizes sizes);

SockEntry* LookupSocket(Handle h);
ProcEntry* LookupProcess(Handle h);
TlsEntry* LookupTls(Handle h);

// Closes the underlying resource (closesocket / process+pipe handles /
// DeleteSecurityContext+FreeCredentialsHandle+closesocket) and forgets
// the handle. Safe to call on an unknown handle (no-op).
void Release(Handle h);

}  // namespace real
}  // namespace w2g

#endif  // W2G_SAPI_HANDLES_H_
