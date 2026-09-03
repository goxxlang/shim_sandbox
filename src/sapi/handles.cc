#include "handles.h"

#include <unordered_map>

namespace w2g {
namespace real {

namespace {

Handle& next_handle() {
  static Handle n = 0;
  return n;
}

std::unordered_map<Handle, SockEntry>& sockets() {
  static std::unordered_map<Handle, SockEntry> m;
  return m;
}
std::unordered_map<Handle, ProcEntry>& processes() {
  static std::unordered_map<Handle, ProcEntry> m;
  return m;
}
std::unordered_map<Handle, TlsEntry>& tls_sessions() {
  static std::unordered_map<Handle, TlsEntry> m;
  return m;
}

}  // namespace

Handle AllocSocket(SOCKET s, bool udp) {
  Handle h = ++next_handle();
  sockets()[h] = SockEntry{s, udp};
  return h;
}

Handle AllocProcess(HANDLE process, HANDLE stdout_read, HANDLE stdin_write) {
  Handle h = ++next_handle();
  processes()[h] = ProcEntry{process, stdout_read, stdin_write};
  return h;
}

Handle AllocTls(SOCKET s, CredHandle cred, CtxtHandle ctx, SecPkgContext_StreamSizes sizes) {
  Handle h = ++next_handle();
  TlsEntry e;
  e.s = s;
  e.cred = cred;
  e.ctx = ctx;
  e.sizes = sizes;
  tls_sessions()[h] = std::move(e);
  return h;
}

SockEntry* LookupSocket(Handle h) {
  auto it = sockets().find(h);
  return it == sockets().end() ? nullptr : &it->second;
}
ProcEntry* LookupProcess(Handle h) {
  auto it = processes().find(h);
  return it == processes().end() ? nullptr : &it->second;
}
TlsEntry* LookupTls(Handle h) {
  auto it = tls_sessions().find(h);
  return it == tls_sessions().end() ? nullptr : &it->second;
}

void Release(Handle h) {
  if (auto it = sockets().find(h); it != sockets().end()) {
    if (it->second.s != INVALID_SOCKET) closesocket(it->second.s);
    sockets().erase(it);
    return;
  }
  if (auto it = processes().find(h); it != processes().end()) {
    if (it->second.stdout_read) CloseHandle(it->second.stdout_read);
    if (it->second.stdin_write) CloseHandle(it->second.stdin_write);
    if (it->second.process) CloseHandle(it->second.process);
    processes().erase(it);
    return;
  }
  if (auto it = tls_sessions().find(h); it != tls_sessions().end()) {
    DeleteSecurityContext(&it->second.ctx);
    FreeCredentialsHandle(&it->second.cred);
    if (it->second.s != INVALID_SOCKET) closesocket(it->second.s);
    tls_sessions().erase(it);
    return;
  }
}

}  // namespace real
}  // namespace w2g
