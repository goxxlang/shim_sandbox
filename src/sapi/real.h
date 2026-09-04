#ifndef W2G_SAPI_REAL_H_
#define W2G_SAPI_REAL_H_

// Real, host-OS-backed implementations of every gocvm topic (see
// docs/architecture.md's topics table). Only reachable when
// W2G_ABAC_SYSTEM=1 (see w2g/system_policy.h) -- W2gSapiHandle (handle.cc)
// is the sole caller, gated the same way os_open/os_create are gated in
// wasigoc's own runtime.hpp.
//
// One real_<platform>.cc provides these; real_posix.cc is an honest
// "not implemented on this platform yet" fallback so the library still
// links on non-Windows hosts. Every function does the real, synchronous
// work itself (connect/bind/CreateProcess/DecryptMessage/...) and
// returns a single reply payload plus a W2gResult -- gocvm.Call's
// existing single-request/single-reply shape (Msg -> Msg); a "live"
// resource (an open socket, a running process, an established TLS
// session) is handed back as an opaque handle (src/sapi/handles.h) that
// later topics reference, rather than a new bidirectional session
// protocol.
//
// Handle-plus-data topics use "<decimal-handle>\x1f<data>", split on the
// *first* 0x1F only -- the same convention Exec's argv already uses, and
// safe for arbitrary binary payloads since the handle prefix itself can
// never contain 0x1F.

#include "w2g/c/types.h"

#include <cstdint>
#include <string>

namespace w2g {
namespace real {

struct Reply {
  W2gResult code = W2G_RESULT_OK;
  std::string payload;
};

// "<network> <address>", e.g. "tcp example.com:80". Resolves the address
// (real DNS) and performs a real connect(); replies "ok handle=<id>
// local=.. remote=.." (the socket is left open, registered in the
// handle table -- see handles.h) or "error: .." with the real OS error
// text.
Reply Dial(const std::string& network, const std::string& address);

// "<network> <address>". Real bind()+listen(), left listening. Replies
// "ok handle=<id> bound=.." (useful for ":0" ephemeral-port requests).
Reply ListenProbe(const std::string& network, const std::string& address);

// "<address>". Real bind() only (no listen), TCP -- pure probe, closed
// immediately, no handle (nothing in stdlib needs a bind-without-listen
// handle).
Reply TcpBindProbe(const std::string& address);

// "<address>". Real bind() only, UDP -- pure probe, same shape as
// TcpBindProbe.
Reply UdpBindProbe(const std::string& address);

// "<handle>". Real accept() on a listening socket (blocks). Replies
// "ok handle=<id> remote=.." for the new connection.
Reply Accept(const std::string& handle);

// "<handle>\x1f<maxlen>". Real recv() (blocks). Reply payload is the
// raw bytes read; on peer close code is OK but payload is empty AND
// the caller should treat it as EOF the same way real recv()==0 means
// EOF (see IoReadIsEof below) -- there is no separate error signal for
// a clean close, only for a real failure.
Reply IoRead(const std::string& handle_and_maxlen);
// Real send() (blocks until queued). Reply payload is the decimal
// count of bytes written.
Reply IoWrite(const std::string& handle_and_data);
// "<handle>\x1f<maxlen>". Real recvfrom(), UDP only. Reply payload is
// "<fromaddr>\x1f<bytes>".
Reply IoReadFrom(const std::string& handle_and_maxlen);
// "<handle>\x1f<addr>\x1f<data>". Real sendto(), UDP only (unconnected).
// Reply payload is the decimal count of bytes written.
Reply IoWriteTo(const std::string& handle_and_addr_and_data);
// "<handle>". Closes a socket handle (TCP or UDP, connected or
// listening) opened by Dial/ListenProbe/Accept/UdpBind-turned-handle.
Reply IoClose(const std::string& handle);

// Fields separated by 0x1F: argv[0], argv[1], ... Real CreateProcess,
// waits for exit, captures combined stdout+stderr (bounded, stdin NUL).
// Reply payload is "exit=<n>\n<output>". Unused by os/exec's Go source
// as of the stdout/stderr split below (kept working, still a valid
// combined-pipe primitive for direct gocvm.Call use) -- Run/Output/
// CombinedOutput all route through Start+Wait instead, which gives
// stdout and stderr independent pipes.
Reply Exec(const std::string& argv_joined);
// Same argv shape as Exec, but does not wait: spawns the process with
// stdout, stderr, and stdin each on their OWN real pipe (unlike Exec's
// combined stdout+stderr and NUL stdin), and replies "ok handle=<id>"
// immediately (os/exec's Start).
Reply ExecStart(const std::string& argv_joined);
// "<handle>". Blocks for the process to exit. Reply "exit=<n>".
Reply ExecWait(const std::string& handle);
// "<handle>\x1f<maxlen>". Real ReadFile on the process's stdout pipe
// (blocks) -- an `os.exec.start` handle only, which (unlike the one-shot
// `Exec` above) gives stdout and stderr separate pipes. Reply payload is
// the raw bytes read; an empty successful reply means the pipe is at EOF
// (process exited and all buffered output drained), same convention as
// IoRead.
Reply ExecStdoutRead(const std::string& handle_and_maxlen);
// Same shape as ExecStdoutRead, but the process's separate stderr pipe.
Reply ExecStderrRead(const std::string& handle_and_maxlen);
// "<handle>\x1f<data>". Real WriteFile onto the process's stdin pipe
// (an `os.exec.start` handle only -- the one-shot `Exec` above still
// wires stdin to NUL, matching os/exec's own doc that stdin needs
// Start). Reply payload is the decimal count of bytes written.
Reply ExecStdinWrite(const std::string& handle_and_data);
// "<handle>". Closes the process's stdin pipe (signals EOF to the
// child) without touching the handle's stdout pipe or process handle --
// distinct from ExecWait/Release, which is why a plain IoClose isn't
// reused here.
Reply ExecStdinClose(const std::string& handle);
// "<file>". Real PATH/%PATHEXT% search (GetEnvironmentVariableW +
// GetFileAttributesW per candidate, current directory NOT implicitly
// searched -- matches modern Go's own hardened LookPath, not classic
// cmd.exe). Replies "ok <resolved path>" or "error: <file>".
Reply ExecLookPath(const std::string& file);

// op_and_arg: "" (current process's user) | "lookup <name>" |
// "lookupid <sid-string>". Real GetUserNameW / NetUserGetInfo /
// LookupAccountNameW+ConvertSidToStringSidW / the reverse for lookupid.
// Reply payload is "<uid>\x1f<username>\x1f<name>\x1f<homedir>".
Reply User(const std::string& op_and_arg);

// "<op>[ <arg>]": getpid | getppid | getenv <name> | environ |
// chdir <dir> | kill <pid> <sig>. Real GetCurrentProcessId / parent-pid
// lookup / GetEnvironmentVariable / GetEnvironmentStrings /
// SetCurrentDirectoryW / OpenProcess+TerminateProcess.
Reply Syscall(const std::string& op_and_arg);

// "<address>". Real TCP connect plus a real TLS handshake (Schannel/
// SSPI -- src/sapi/tls_win.cc), automatic certificate chain + hostname
// validation on (never disabled). Replies "ok handle=<id>" for an
// established session, or "error: .." for a connect or handshake
// failure (including a genuine certificate validation failure -- this
// is a real security boundary, not a demo toggle).
Reply TlsDial(const std::string& address);
// Same shape as IoRead/IoWrite/IoClose, wrapping DecryptMessage/
// EncryptMessage around the same underlying socket.
Reply TlsIoRead(const std::string& handle_and_maxlen);
Reply TlsIoWrite(const std::string& handle_and_data);
Reply TlsIoClose(const std::string& handle);

}  // namespace real
}  // namespace w2g

#endif  // W2G_SAPI_REAL_H_
