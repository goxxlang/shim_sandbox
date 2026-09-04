#include "handles.h"

#include <mutex>
#include <unordered_map>

namespace w2g {
namespace real {

namespace {

// Guards next_handle() and all three maps below -- see handles.h's doc
// comment for why this is needed now (a worker pool, not one thread) and
// why a looked-up pointer stays safe to use after the lock is released.
std::mutex& mu() {
  static std::mutex m;
  return m;
}

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
  std::lock_guard<std::mutex> lk(mu());
  Handle h = ++next_handle();
  sockets()[h] = SockEntry{s, udp};
  return h;
}

Handle AllocProcess(HANDLE process, HANDLE stdout_read, HANDLE stdin_write) {
  std::lock_guard<std::mutex> lk(mu());
  Handle h = ++next_handle();
  processes()[h] = ProcEntry{process, stdout_read, stdin_write};
  return h;
}

Handle AllocTls(SOCKET s, CredHandle cred, CtxtHandle ctx, SecPkgContext_StreamSizes sizes) {
  std::lock_guard<std::mutex> lk(mu());
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
  std::lock_guard<std::mutex> lk(mu());
  auto it = sockets().find(h);
  return it == sockets().end() ? nullptr : &it->second;
}
ProcEntry* LookupProcess(Handle h) {
  std::lock_guard<std::mutex> lk(mu());
  auto it = processes().find(h);
  return it == processes().end() ? nullptr : &it->second;
}
TlsEntry* LookupTls(Handle h) {
  std::lock_guard<std::mutex> lk(mu());
  auto it = tls_sessions().find(h);
  return it == tls_sessions().end() ? nullptr : &it->second;
}

void Release(Handle h) {
  std::lock_guard<std::mutex> lk(mu());
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

void CloseProcessStdin(Handle h) {
  std::lock_guard<std::mutex> lk(mu());
  auto it = processes().find(h);
  if (it == processes().end()) return;
  if (it->second.stdin_write) {
    CloseHandle(it->second.stdin_write);
    it->second.stdin_write = nullptr;
  }
}

}  // namespace real
}  // namespace w2g
