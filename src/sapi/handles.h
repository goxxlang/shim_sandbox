#ifndef W2G_SAPI_HANDLES_H_
#define W2G_SAPI_HANDLES_H_

// Windows-only (real_win.cc / tls_win.cc's own home). Live resources a
// gocvm topic handed back a handle for: an open socket, a running
// process, an established TLS session.
//
// Thread-safe: gocvm_bridge.cc's AsyncSapiBridge runs a small pool of
// worker threads (not one), so two topic calls on two different handles
// -- e.g. os/exec's concurrent stdin-write and stdout-read pump on the
// same process handle -- can genuinely run at once. Alloc*/Lookup*/
// Release all take a single internal mutex around the map operation
// itself (see handles.cc); a looked-up pointer stays valid afterward
// without the lock held, since std::unordered_map never invalidates a
// live element's reference/pointer except by erasing that exact element
// (a standard guarantee, unaffected by concurrent inserts/rehashing).
// The one residual caller obligation: don't Release(h) while another
// call on that same h is still in flight (a concurrent Close() racing a
// blocked Read() on the same handle) -- every existing caller in this
// codebase already sequences its own calls to avoid that (os/exec's
// Wait() joins its stdout-pump/stdin-write goroutines before triggering
// ExecWait's Release), so this is a documented limitation, not a bug to
// silently work around.

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

// Closes just a process handle's stdin pipe (signals EOF to the child)
// and clears ProcEntry::stdin_write, under the same lock Release uses --
// unlike Release, the process/stdout_read handles and the map entry
// itself stay live. Mutating stdin_write via a LookupProcess() pointer
// directly, without this, would race Release's own read of that field.
void CloseProcessStdin(Handle h);

}  // namespace real
}  // namespace w2g

#endif  // W2G_SAPI_HANDLES_H_
