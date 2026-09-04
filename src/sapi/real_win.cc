// Real, Windows-backed implementations of every gocvm topic. Only
// compiled when W2G_ABAC_SYSTEM=1 gates a caller into src/sapi/handle.cc
// reaching these at all -- see real.h.
//
// Every OS call here (recv/accept/WaitForSingleObject/ReadFile/...) is a
// genuine blocking call, same as the original Exec()/Dial() this session
// already shipped. That's deliberate, not an oversight: wasigo has one
// OS thread and a cooperative scheduler with no real preemption (see
// runtime.hpp's own header comment) -- a blocking gocvm.Call already
// freezes the whole process until it returns, so there is nothing extra
// to get right here. The one real consequence: two Go++ goroutines in
// the *same* process cannot rendezvous through a blocking Accept()+
// Dial() pair (whichever runs first never yields back to the scheduler
// for the other to run) -- a genuine, pre-existing limit of the
// cooperative model, not new to this file. A real client/server pair
// needs two processes, same as it would on any single-threaded runtime.
#include "handles.h"
#include "net_internal.h"
#include "real.h"

#include <tlhelp32.h>
#include <lm.h>
#include <sddl.h>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// MSVC/clang-cl honor this; mingw/GNU ld ignores it silently -- the mingw
// build links ws2_32/iphlpapi/secur32/crypt32 explicitly (CMakeLists.txt
// / goclang++.bat).
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "netapi32.lib")
#endif

namespace w2g {
namespace real {

using namespace internal;  // NOLINT -- EnsureWinsock/WinErr/Utf8ToWide/.../ConnectReal (net_internal.h)

namespace {

// MS-documented CreateProcess argv quoting: only quote when needed
// (whitespace/quote/empty) so plain tokens like "cmd.exe" or "/c" stay
// bare -- cmd.exe's own /c re-parsing does its own quote-stripping on
// the raw tail and misbehaves if every token carries redundant quotes.
std::string QuoteArg(const std::string& arg) {
  if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
    return arg;
  }
  std::string out = "\"";
  for (size_t i = 0; i < arg.size();) {
    size_t backslashes = 0;
    while (i < arg.size() && arg[i] == '\\') {
      ++backslashes;
      ++i;
    }
    if (i == arg.size()) {
      out.append(backslashes * 2, '\\');
      break;
    }
    if (arg[i] == '"') {
      out.append(backslashes * 2 + 1, '\\');
      out.push_back('"');
    } else {
      out.append(backslashes, '\\');
      out.push_back(arg[i]);
    }
    ++i;
  }
  out.push_back('"');
  return out;
}

// do_listen: real listen() after bind. keep_open: leave the socket open
// and registered in the handle table (reply carries "handle=<id>")
// instead of closing it immediately -- true for ListenProbe (needs a
// handle to Accept() on later), false for the plain Tcp/UdpBindProbe
// (pure reachability probes, nothing downstream needs the socket).
Reply BindReal(const std::string& network, const std::string& address, bool do_listen,
              bool keep_open) {
  EnsureWinsock();
  std::string host, port;
  if (!SplitHostPort(address, &host, &port)) {
    return {W2G_RESULT_INVALID_ARGUMENT, "error: missing port in address " + address};
  }
  addrinfo hints{};
  hints.ai_family = Family(network);
  hints.ai_socktype = SockType(network);
  hints.ai_flags = AI_PASSIVE;
  addrinfo* res = nullptr;
  const char* node = host.empty() || host == "0.0.0.0" || host == "*" ? nullptr : host.c_str();
  int rc = getaddrinfo(node, port.c_str(), &hints, &res);
  if (rc != 0) {
    return {W2G_RESULT_OK, "error: resolve " + address + ": " + gai_strerrorA(rc)};
  }
  SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (s == INVALID_SOCKET) {
    int err = WSAGetLastError();
    freeaddrinfo(res);
    return {W2G_RESULT_OK, "error: socket: " + WinErr(err)};
  }
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
  if (bind(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
    int err = WSAGetLastError();
    freeaddrinfo(res);
    closesocket(s);
    return {W2G_RESULT_OK, "error: bind " + address + ": " + WinErr(err)};
  }
  freeaddrinfo(res);
  // listen() is a TCP-only concept (undefined/rejected on SOCK_DGRAM) --
  // a UDP "listener" is just a bound socket ready for recvfrom/sendto,
  // so net.listen with network "udp" (net.ListenPacket's real path)
  // skips this syscall entirely rather than trying and failing it.
  if (do_listen && SockType(network) == SOCK_STREAM && listen(s, SOMAXCONN) != 0) {
    int err = WSAGetLastError();
    closesocket(s);
    return {W2G_RESULT_OK, "error: listen " + address + ": " + WinErr(err)};
  }
  sockaddr_storage bound{};
  int blen = sizeof(bound);
  getsockname(s, reinterpret_cast<sockaddr*>(&bound), &blen);
  std::string desc = FormatSockAddr(bound);
  if (!keep_open) {
    closesocket(s);
    return {W2G_RESULT_OK, "ok bound=" + desc};
  }
  Handle h = AllocSocket(s, SockType(network) == SOCK_DGRAM);
  return {W2G_RESULT_OK, "ok handle=" + std::to_string(h) + " bound=" + desc};
}

}  // namespace

Reply Dial(const std::string& network, const std::string& address) {
  SOCKET s = INVALID_SOCKET;
  Reply r = ConnectReal(network, address, &s);
  if (s == INVALID_SOCKET) return r;
  Handle h = AllocSocket(s, SockType(network) == SOCK_DGRAM);
  // r.payload is "ok local=.. remote=.." -- splice the handle in right
  // after "ok " so it reads the same shape TlsDial/Accept reply with.
  return {W2G_RESULT_OK, "ok handle=" + std::to_string(h) + " " + r.payload.substr(3)};
}

Reply ListenProbe(const std::string& network, const std::string& address) {
  return BindReal(network, address, /*do_listen=*/true, /*keep_open=*/true);
}

Reply TcpBindProbe(const std::string& address) {
  return BindReal("tcp", address, /*do_listen=*/false, /*keep_open=*/false);
}

Reply UdpBindProbe(const std::string& address) {
  return BindReal("udp", address, /*do_listen=*/false, /*keep_open=*/false);
}

Reply Accept(const std::string& handle) {
  Handle h = 0;
  if (!ParseHandle(handle, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  SockEntry* e = LookupSocket(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + handle};
  sockaddr_storage remote{};
  int rlen = sizeof(remote);
  SOCKET c = accept(e->s, reinterpret_cast<sockaddr*>(&remote), &rlen);
  if (c == INVALID_SOCKET) {
    return {W2G_RESULT_OK, "error: accept: " + WinErr(WSAGetLastError())};
  }
  Handle nh = AllocSocket(c, false);
  return {W2G_RESULT_OK, "ok handle=" + std::to_string(nh) + " remote=" + FormatSockAddr(remote)};
}

Reply IoRead(const std::string& handle_and_maxlen) {
  std::string hs, ms;
  SplitOne(handle_and_maxlen, '\x1f', &hs, &ms);
  Handle h = 0;
  if (!ParseHandle(hs, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  SockEntry* e = LookupSocket(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + hs};
  long maxlen = std::strtol(ms.c_str(), nullptr, 10);
  if (maxlen <= 0 || maxlen > 60000) maxlen = 60000;
  std::vector<char> buf(static_cast<size_t>(maxlen));
  int n = recv(e->s, buf.data(), maxlen, 0);
  if (n < 0) return {W2G_RESULT_OK, "error: read: " + WinErr(WSAGetLastError())};
  // n == 0: real recv() EOF (peer closed) -- an empty OK payload is the
  // read-topics' EOF convention (a live recv can never legitimately
  // return 0 bytes any other way).
  return {W2G_RESULT_OK, std::string(buf.data(), static_cast<size_t>(n))};
}

Reply IoWrite(const std::string& handle_and_data) {
  std::string hs, data;
  SplitOne(handle_and_data, '\x1f', &hs, &data);
  Handle h = 0;
  if (!ParseHandle(hs, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  SockEntry* e = LookupSocket(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + hs};
  size_t sent = 0;
  while (sent < data.size()) {
    int n = send(e->s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
    if (n <= 0) return {W2G_RESULT_OK, "error: write: " + WinErr(WSAGetLastError())};
    sent += static_cast<size_t>(n);
  }
  return {W2G_RESULT_OK, std::to_string(sent)};
}

Reply IoReadFrom(const std::string& handle_and_maxlen) {
  std::string hs, ms;
  SplitOne(handle_and_maxlen, '\x1f', &hs, &ms);
  Handle h = 0;
  if (!ParseHandle(hs, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  SockEntry* e = LookupSocket(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + hs};
  long maxlen = std::strtol(ms.c_str(), nullptr, 10);
  if (maxlen <= 0 || maxlen > 60000) maxlen = 60000;
  std::vector<char> buf(static_cast<size_t>(maxlen));
  sockaddr_storage from{};
  int flen = sizeof(from);
  int n = recvfrom(e->s, buf.data(), maxlen, 0, reinterpret_cast<sockaddr*>(&from), &flen);
  if (n < 0) return {W2G_RESULT_OK, "error: readfrom: " + WinErr(WSAGetLastError())};
  return {W2G_RESULT_OK, FormatSockAddr(from) + "\x1f" + std::string(buf.data(), static_cast<size_t>(n))};
}

Reply IoWriteTo(const std::string& handle_and_addr_and_data) {
  std::string hs, rest;
  SplitOne(handle_and_addr_and_data, '\x1f', &hs, &rest);
  std::string addr, data;
  SplitOne(rest, '\x1f', &addr, &data);
  Handle h = 0;
  if (!ParseHandle(hs, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  SockEntry* e = LookupSocket(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + hs};
  std::string host, port;
  if (!SplitHostPort(addr, &host, &port)) {
    return {W2G_RESULT_INVALID_ARGUMENT, "error: missing port in address " + addr};
  }
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
    return {W2G_RESULT_OK, "error: resolve " + addr};
  }
  int n = sendto(e->s, data.data(), static_cast<int>(data.size()), 0, res->ai_addr,
                 static_cast<int>(res->ai_addrlen));
  freeaddrinfo(res);
  if (n < 0) return {W2G_RESULT_OK, "error: writeto: " + WinErr(WSAGetLastError())};
  return {W2G_RESULT_OK, std::to_string(n)};
}

Reply IoClose(const std::string& handle) {
  Handle h = 0;
  if (!ParseHandle(handle, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  Release(h);
  return {W2G_RESULT_OK, "ok"};
}

namespace {

std::vector<std::string> SplitArgv(const std::string& argv_joined) {
  std::vector<std::string> fields;
  size_t start = 0;
  for (;;) {
    size_t i = argv_joined.find('\x1f', start);
    fields.push_back(argv_joined.substr(start, i == std::string::npos ? i : i - start));
    if (i == std::string::npos) break;
    start = i + 1;
  }
  return fields;
}

// Launches argv with combined stdout+stderr redirected to a pipe this
// returns the read end of (write end closed in the parent already).
// When with_stdin is false, the child's stdin is wired to NUL (no
// interactive input -- the original one-shot Exec's behavior, which has
// no way to stream input during its single blocking call). When true,
// *out_stdin_write receives the parent's write end of a real stdin pipe
// (os/exec's Start, which can stream input via ExecStdinWrite below).
// Caller owns *out_process/*out_read/*out_stdin_write on success.
bool LaunchProcess(const std::string& argv_joined, bool with_stdin, PROCESS_INFORMATION* out_pi,
                   HANDLE* out_read, HANDLE* out_stdin_write, std::string* err_out) {
  auto fields = SplitArgv(argv_joined);
  if (fields.empty() || fields[0].empty()) {
    *err_out = "error: empty argv";
    return false;
  }
  std::ostringstream cmd;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) cmd << ' ';
    cmd << QuoteArg(fields[i]);
  }
  std::wstring wcmd = Utf8ToWide(cmd.str());

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE read_end = nullptr, write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
    *err_out = "error: CreatePipe: " + WinErr(static_cast<int>(GetLastError()));
    return false;
  }
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  HANDLE stdin_read = nullptr, stdin_write = nullptr, nul_in = nullptr;
  if (with_stdin) {
    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
      *err_out = "error: CreatePipe: " + WinErr(static_cast<int>(GetLastError()));
      CloseHandle(read_end);
      CloseHandle(write_end);
      return false;
    }
    SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);
  } else {
    nul_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                         OPEN_EXISTING, 0, nullptr);
  }

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = write_end;
  si.hStdError = write_end;
  si.hStdInput = with_stdin ? stdin_read : nul_in;
  PROCESS_INFORMATION pi{};

  BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                           &pi);
  CloseHandle(write_end);
  if (nul_in != INVALID_HANDLE_VALUE && nul_in != nullptr) CloseHandle(nul_in);
  if (stdin_read) CloseHandle(stdin_read);
  if (!ok) {
    *err_out = "error: CreateProcess " + fields[0] + ": " +
              WinErr(static_cast<int>(GetLastError()));
    CloseHandle(read_end);
    if (stdin_write) CloseHandle(stdin_write);
    return false;
  }
  *out_pi = pi;
  *out_read = read_end;
  if (out_stdin_write) *out_stdin_write = stdin_write;
  return true;
}

// Independent from LaunchProcess above (which Exec still uses for its
// combined-pipe, stdin-to-NUL one-shot shape): os/exec's Start needs
// stdout and stderr on two SEPARATE pipes (real Go's own Cmd.Output only
// ever captures stdout; CombinedOutput's non-deterministic interleaving
// of two independently-read pipes matches real Go's actual behavior,
// which also uses two separate os.Pipe()s under the hood), plus the
// already-existing real stdin pipe.
bool LaunchProcessSplit(const std::string& argv_joined, PROCESS_INFORMATION* out_pi,
                        HANDLE* out_stdout_read, HANDLE* out_stderr_read, HANDLE* out_stdin_write,
                        std::string* err_out) {
  auto fields = SplitArgv(argv_joined);
  if (fields.empty() || fields[0].empty()) {
    *err_out = "error: empty argv";
    return false;
  }
  std::ostringstream cmd;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) cmd << ' ';
    cmd << QuoteArg(fields[i]);
  }
  std::wstring wcmd = Utf8ToWide(cmd.str());

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE stdout_read = nullptr, stdout_write = nullptr;
  HANDLE stderr_read = nullptr, stderr_write = nullptr;
  HANDLE stdin_read = nullptr, stdin_write = nullptr;
  if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0) ||
      !CreatePipe(&stderr_read, &stderr_write, &sa, 0) ||
      !CreatePipe(&stdin_read, &stdin_write, &sa, 0)) {
    *err_out = "error: CreatePipe: " + WinErr(static_cast<int>(GetLastError()));
    if (stdout_read) CloseHandle(stdout_read);
    if (stdout_write) CloseHandle(stdout_write);
    if (stderr_read) CloseHandle(stderr_read);
    if (stderr_write) CloseHandle(stderr_write);
    if (stdin_read) CloseHandle(stdin_read);
    if (stdin_write) CloseHandle(stdin_write);
    return false;
  }
  SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = stdout_write;
  si.hStdError = stderr_write;
  si.hStdInput = stdin_read;
  PROCESS_INFORMATION pi{};

  BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
                           &pi);
  CloseHandle(stdout_write);
  CloseHandle(stderr_write);
  CloseHandle(stdin_read);
  if (!ok) {
    *err_out = "error: CreateProcess " + fields[0] + ": " +
              WinErr(static_cast<int>(GetLastError()));
    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    CloseHandle(stdin_write);
    return false;
  }
  *out_pi = pi;
  *out_stdout_read = stdout_read;
  *out_stderr_read = stderr_read;
  *out_stdin_write = stdin_write;
  return true;
}

}  // namespace

Reply Exec(const std::string& argv_joined) {
  PROCESS_INFORMATION pi{};
  HANDLE read_end = nullptr;
  std::string err;
  if (!LaunchProcess(argv_joined, false, &pi, &read_end, nullptr, &err)) return {W2G_RESULT_OK, err};

  std::string out;
  char buf[4096];
  DWORD n = 0;
  constexpr size_t kCap = 1u << 16;
  while (out.size() < kCap && ReadFile(read_end, buf, sizeof(buf), &n, nullptr) && n > 0) {
    out.append(buf, n);
  }
  CloseHandle(read_end);

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  std::ostringstream o;
  o << "exit=" << exit_code << "\n" << out;
  return {W2G_RESULT_OK, o.str()};
}

Reply ExecStart(const std::string& argv_joined) {
  PROCESS_INFORMATION pi{};
  HANDLE stdout_read = nullptr, stderr_read = nullptr, stdin_write = nullptr;
  std::string err;
  if (!LaunchProcessSplit(argv_joined, &pi, &stdout_read, &stderr_read, &stdin_write, &err)) {
    return {W2G_RESULT_OK, err};
  }
  CloseHandle(pi.hThread);
  Handle h = AllocProcess(pi.hProcess, stdout_read, stderr_read, stdin_write);
  return {W2G_RESULT_OK, "ok handle=" + std::to_string(h)};
}

Reply ExecWait(const std::string& handle) {
  Handle h = 0;
  if (!ParseHandle(handle, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  ProcEntry* e = LookupProcess(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + handle};
  WaitForSingleObject(e->process, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(e->process, &exit_code);
  Release(h);
  return {W2G_RESULT_OK, "exit=" + std::to_string(exit_code)};
}

namespace {
Reply ExecStreamRead(const std::string& handle_and_maxlen, HANDLE ProcEntry::*stream) {
  std::string hs, ms;
  SplitOne(handle_and_maxlen, '\x1f', &hs, &ms);
  Handle h = 0;
  if (!ParseHandle(hs, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  ProcEntry* e = LookupProcess(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + hs};
  long maxlen = std::strtol(ms.c_str(), nullptr, 10);
  if (maxlen <= 0 || maxlen > 60000) maxlen = 60000;
  std::vector<char> buf(static_cast<size_t>(maxlen));
  DWORD n = 0;
  // ReadFile on a pipe returns FALSE (ERROR_BROKEN_PIPE) once the write
  // end (the child's redirected stdout/stderr) is fully closed -- same
  // EOF convention as IoRead's recv()==0: an empty OK payload.
  if (!ReadFile(e->*stream, buf.data(), static_cast<DWORD>(maxlen), &n, nullptr)) {
    DWORD err = GetLastError();
    if (err == ERROR_BROKEN_PIPE) return {W2G_RESULT_OK, ""};
    return {W2G_RESULT_OK, "error: read: " + WinErr(static_cast<int>(err))};
  }
  return {W2G_RESULT_OK, std::string(buf.data(), n)};
}
}  // namespace

Reply ExecStdoutRead(const std::string& handle_and_maxlen) {
  return ExecStreamRead(handle_and_maxlen, &ProcEntry::stdout_read);
}

Reply ExecStderrRead(const std::string& handle_and_maxlen) {
  return ExecStreamRead(handle_and_maxlen, &ProcEntry::stderr_read);
}

Reply ExecStdinWrite(const std::string& handle_and_data) {
  std::string hs, data;
  SplitOne(handle_and_data, '\x1f', &hs, &data);
  Handle h = 0;
  if (!ParseHandle(hs, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  ProcEntry* e = LookupProcess(h);
  if (!e) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + hs};
  if (!e->stdin_write) return {W2G_RESULT_OK, "error: stdin not piped (process not started with a stdin pipe, or already closed)"};
  size_t sent = 0;
  while (sent < data.size()) {
    DWORD n = 0;
    if (!WriteFile(e->stdin_write, data.data() + sent, static_cast<DWORD>(data.size() - sent), &n,
                   nullptr)) {
      return {W2G_RESULT_OK, "error: write: " + WinErr(static_cast<int>(GetLastError()))};
    }
    sent += n;
  }
  return {W2G_RESULT_OK, std::to_string(sent)};
}

Reply ExecStdinClose(const std::string& handle) {
  Handle h = 0;
  if (!ParseHandle(handle, &h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: bad handle"};
  if (!LookupProcess(h)) return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown handle " + handle};
  CloseProcessStdin(h);
  return {W2G_RESULT_OK, "ok"};
}

namespace {

// Splits on sep, dropping empty fields (";;" or a leading/trailing ";"
// contribute nothing) -- both %PATHEXT% and %PATH% use this convention.
std::vector<std::wstring> SplitEnvList(const std::wstring& s, wchar_t sep) {
  std::vector<std::wstring> out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t p = s.find(sep, start);
    std::wstring tok = s.substr(start, p == std::wstring::npos ? std::wstring::npos : p - start);
    if (!tok.empty()) out.push_back(tok);
    if (p == std::wstring::npos) break;
    start = p + 1;
  }
  return out;
}

std::wstring GetEnvW(const wchar_t* name, const wchar_t* fallback) {
  constexpr DWORD kCap = 32768;
  wchar_t buf[kCap];
  DWORD n = GetEnvironmentVariableW(name, buf, kCap);
  return (n > 0 && n < kCap) ? std::wstring(buf, n) : std::wstring(fallback);
}

bool FileExistsNotDir(const std::wstring& path) {
  DWORD attrs = GetFileAttributesW(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// A trailing "." after the last path separator counts as an extension
// (matches real Go's own lp_windows.go hasExt, which just checks for
// a '.' in the final path element -- it doesn't validate the extension
// against PATHEXT here, only decides whether to append one at all).
bool HasExtension(const std::string& name) {
  size_t slash = name.find_last_of("/\\");
  size_t dot = name.find_last_of('.');
  return dot != std::string::npos && (slash == std::string::npos || dot > slash);
}

}  // namespace

// Real PATH/PATHEXT search, Windows semantics: current directory is NOT
// implicitly searched (only explicit "." or "..\" entries in PATH would
// hit it) -- matches modern Go's own security-hardened LookPath, not
// classic cmd.exe. Bounded relative to real Go's actual algorithm: no
// ErrDot relative-path diagnostic, no UNC-path special-casing. A name
// with an extension already (a '.' in its final path element) is
// checked as-is; one without tries each %PATHEXT% entry (default
// ".COM;.EXE;.BAT;.CMD" if unset, same default real Go uses) in order.
Reply ExecLookPath(const std::string& file) {
  if (file.empty()) return {W2G_RESULT_OK, "error: " + file};
  std::wstring wfile = Utf8ToWide(file);
  bool has_ext = HasExtension(file);
  bool has_sep = file.find_first_of("/\\") != std::string::npos;
  std::vector<std::wstring> exts;
  if (!has_ext) exts = SplitEnvList(GetEnvW(L"PATHEXT", L".COM;.EXE;.BAT;.CMD"), L';');

  auto tryBase = [&](const std::wstring& base) -> std::wstring {
    if (has_ext) return FileExistsNotDir(base) ? base : L"";
    for (const auto& ext : exts) {
      std::wstring cand = base + ext;
      if (FileExistsNotDir(cand)) return cand;
    }
    return L"";
  };

  if (has_sep) {
    std::wstring found = tryBase(wfile);
    if (!found.empty()) return {W2G_RESULT_OK, "ok " + WideToUtf8(found.c_str())};
    return {W2G_RESULT_OK, "error: " + file};
  }

  for (const auto& dir : SplitEnvList(GetEnvW(L"PATH", L""), L';')) {
    std::wstring base = dir;
    if (base.back() != L'\\' && base.back() != L'/') base += L'\\';
    base += wfile;
    std::wstring found = tryBase(base);
    if (!found.empty()) return {W2G_RESULT_OK, "ok " + WideToUtf8(found.c_str())};
  }
  return {W2G_RESULT_OK, "error: " + file};
}

namespace {

// user.Uid/Name/HomeDir shape: user.Gid is the primary group's SID,
// built the same way real Go's own os/user Windows implementation does
// (NetUserGetInfo level 4's usri4_primary_group_id is a RID -- swap the
// user SID's last "-<rid>" segment for it, string-form -- SIDs are
// hierarchical, "S-1-5-21-a-b-c-<rid>", so this is exactly the group
// SID without needing raw SID-struct manipulation).
Reply UserReplyFromSid(PSID sid, const std::string& username) {
  wchar_t* sid_str = nullptr;
  if (!ConvertSidToStringSidW(sid, &sid_str)) {
    return {W2G_RESULT_OK, "error: ConvertSidToStringSid: " + WinErr(static_cast<int>(GetLastError()))};
  }
  std::string uid = WideToUtf8(sid_str);
  LocalFree(sid_str);

  std::string gid = uid;
  std::string home;
  LPUSER_INFO_4 ui = nullptr;
  std::wstring wuser = Utf8ToWide(username);
  if (NetUserGetInfo(nullptr, wuser.c_str(), 4, reinterpret_cast<LPBYTE*>(&ui)) == NERR_Success &&
      ui) {
    auto last_dash = uid.rfind('-');
    if (last_dash != std::string::npos) {
      gid = uid.substr(0, last_dash + 1) + std::to_string(ui->usri4_primary_group_id);
    }
    if (ui->usri4_home_dir && ui->usri4_home_dir[0]) home = WideToUtf8(ui->usri4_home_dir);
    NetApiBufferFree(ui);
  }
  if (home.empty()) home = "C:\\Users\\" + username;  // local-account convention

  return {W2G_RESULT_OK, uid + "\x1f" + username + "\x1f" + username + "\x1f" + home};
}

Reply LookupUserByName(const std::string& username) {
  std::wstring wname = Utf8ToWide(username);
  uint8_t sidbuf[SECURITY_MAX_SID_SIZE];
  DWORD sidlen = sizeof(sidbuf);
  wchar_t domain[256];
  DWORD domlen = 256;
  SID_NAME_USE use;
  if (!LookupAccountNameW(nullptr, wname.c_str(), sidbuf, &sidlen, domain, &domlen, &use)) {
    return {W2G_RESULT_OK, "error: LookupAccountName " + username + ": " +
                              WinErr(static_cast<int>(GetLastError()))};
  }
  return UserReplyFromSid(reinterpret_cast<PSID>(sidbuf), username);
}

Reply LookupUserById(const std::string& sid_string) {
  PSID sid = nullptr;
  std::wstring wsid = Utf8ToWide(sid_string);
  if (!ConvertStringSidToSidW(wsid.c_str(), &sid)) {
    return {W2G_RESULT_INVALID_ARGUMENT, "error: bad sid " + sid_string};
  }
  wchar_t name[256], domain[256];
  DWORD namelen = 256, domlen = 256;
  SID_NAME_USE use;
  if (!LookupAccountSidW(nullptr, sid, name, &namelen, domain, &domlen, &use)) {
    std::string err =
        "error: LookupAccountSid " + sid_string + ": " + WinErr(static_cast<int>(GetLastError()));
    LocalFree(sid);
    return {W2G_RESULT_OK, err};
  }
  Reply r = UserReplyFromSid(sid, WideToUtf8(name));
  LocalFree(sid);
  return r;
}

}  // namespace

// op_and_arg: "" (current process's user) | "lookup <name>" |
// "lookupid <sid-string>".
Reply User(const std::string& op_and_arg) {
  std::string op, arg;
  SplitOne(op_and_arg, ' ', &op, &arg);
  if (op.empty()) {
    wchar_t buf[256];
    DWORD n = 256;
    if (!GetUserNameW(buf, &n)) {
      return {W2G_RESULT_OK, "error: GetUserName: " + WinErr(static_cast<int>(GetLastError()))};
    }
    return LookupUserByName(WideToUtf8(buf, static_cast<int>(n) - 1));
  }
  if (op == "lookup") {
    if (arg.empty()) return {W2G_RESULT_INVALID_ARGUMENT, "error: lookup needs a username"};
    return LookupUserByName(arg);
  }
  if (op == "lookupid") {
    if (arg.empty()) return {W2G_RESULT_INVALID_ARGUMENT, "error: lookupid needs a sid"};
    return LookupUserById(arg);
  }
  return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown os.user op '" + op + "'"};
}

Reply Syscall(const std::string& op_and_arg) {
  std::string op, arg;
  SplitOne(op_and_arg, ' ', &op, &arg);
  if (op == "getpid") {
    return {W2G_RESULT_OK, std::to_string(GetCurrentProcessId())};
  }
  if (op == "getppid") {
    DWORD pid = GetCurrentProcessId();
    DWORD ppid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
      PROCESSENTRY32W entry{};
      entry.dwSize = sizeof(entry);
      if (Process32FirstW(snap, &entry)) {
        do {
          if (entry.th32ProcessID == pid) {
            ppid = entry.th32ParentProcessID;
            break;
          }
        } while (Process32NextW(snap, &entry));
      }
      CloseHandle(snap);
    }
    return {W2G_RESULT_OK, std::to_string(ppid)};
  }
  if (op == "getenv") {
    std::wstring wname = Utf8ToWide(arg);
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(wname.c_str(), buf, 32768);
    if (n == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
      return {W2G_RESULT_OK, "0|"};
    }
    return {W2G_RESULT_OK, "1|" + WideToUtf8(buf, static_cast<int>(n))};
  }
  if (op == "environ") {
    LPWCH block = GetEnvironmentStringsW();
    std::string out;
    if (block) {
      const wchar_t* p = block;
      while (*p) {
        size_t wlen = wcslen(p);
        if (!out.empty()) out.push_back('\x1f');
        out += WideToUtf8(p, static_cast<int>(wlen));
        p += wlen + 1;
      }
      FreeEnvironmentStringsW(block);
    }
    return {W2G_RESULT_OK, out};
  }
  if (op == "chdir") {
    if (arg.empty()) return {W2G_RESULT_INVALID_ARGUMENT, "error: chdir needs a path"};
    if (!SetCurrentDirectoryW(Utf8ToWide(arg).c_str())) {
      return {W2G_RESULT_OK, "error: chdir " + arg + ": " + WinErr(static_cast<int>(GetLastError()))};
    }
    return {W2G_RESULT_OK, "ok"};
  }
  if (op == "kill") {
    std::string pid_s, sig_s;
    SplitOne(arg, ' ', &pid_s, &sig_s);
    if (pid_s.empty()) return {W2G_RESULT_INVALID_ARGUMENT, "error: kill needs a pid"};
    DWORD pid = static_cast<DWORD>(std::strtoul(pid_s.c_str(), nullptr, 10));
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) {
      return {W2G_RESULT_OK, "error: kill " + pid_s + ": " + WinErr(static_cast<int>(GetLastError()))};
    }
    // Windows has no signal-number concept; any real syscall.Kill(pid,
    // sig) terminates the process, same as it would with SIGKILL.
    BOOL ok = TerminateProcess(h, 1);
    int err = ok ? 0 : static_cast<int>(GetLastError());
    CloseHandle(h);
    if (!ok) return {W2G_RESULT_OK, "error: kill " + pid_s + ": " + WinErr(err)};
    return {W2G_RESULT_OK, "ok"};
  }
  return {W2G_RESULT_INVALID_ARGUMENT, "error: unknown syscall op '" + op + "'"};
}

Reply TlsDialProbe(const std::string& address) {
  SOCKET s = INVALID_SOCKET;
  Reply r = ConnectReal("tcp", address, &s);
  if (s != INVALID_SOCKET) closesocket(s);
  if (r.payload.rfind("ok ", 0) == 0) {
    return {W2G_RESULT_UNIMPLEMENTED,
            "tls: tcp connect to " + address + " succeeded (" + r.payload +
                "); TLS handshake not implemented in shim_sandbox yet"};
  }
  return {W2G_RESULT_UNIMPLEMENTED, "tls: " + r.payload};
}

}  // namespace real
}  // namespace w2g
