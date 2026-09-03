// Portability fallback: real_win.cc/tls_win.cc have the actual
// implementations (Winsock/Win32/Schannel). This workspace only builds/
// tests on Windows -- these stay honest "not implemented on this
// platform yet" rather than a half-tested POSIX port.
#include "real.h"

namespace w2g {
namespace real {

namespace {
Reply NotPortedYet(const char* what) {
  return {W2G_RESULT_UNIMPLEMENTED,
          std::string("error: ") + what + " not implemented on this platform yet"};
}
}  // namespace

Reply Dial(const std::string&, const std::string&) { return NotPortedYet("net.dial"); }
Reply ListenProbe(const std::string&, const std::string&) { return NotPortedYet("net.listen"); }
Reply TcpBindProbe(const std::string&) { return NotPortedYet("net.tcp.bind"); }
Reply UdpBindProbe(const std::string&) { return NotPortedYet("net.udp.bind"); }
Reply Accept(const std::string&) { return NotPortedYet("net.accept"); }
Reply IoRead(const std::string&) { return NotPortedYet("net.io.read"); }
Reply IoWrite(const std::string&) { return NotPortedYet("net.io.write"); }
Reply IoReadFrom(const std::string&) { return NotPortedYet("net.io.readfrom"); }
Reply IoWriteTo(const std::string&) { return NotPortedYet("net.io.writeto"); }
Reply IoClose(const std::string&) { return NotPortedYet("net.io.close"); }
Reply Exec(const std::string&) { return NotPortedYet("os.exec"); }
Reply ExecStart(const std::string&) { return NotPortedYet("os.exec.start"); }
Reply ExecWait(const std::string&) { return NotPortedYet("os.exec.wait"); }
Reply ExecStdoutRead(const std::string&) { return NotPortedYet("os.exec.stdout.read"); }
Reply User(const std::string&) { return NotPortedYet("os.user"); }
Reply Syscall(const std::string&) { return NotPortedYet("syscall"); }
Reply TlsDial(const std::string&) { return NotPortedYet("tls.dial"); }
Reply TlsIoRead(const std::string&) { return NotPortedYet("tls.io.read"); }
Reply TlsIoWrite(const std::string&) { return NotPortedYet("tls.io.write"); }
Reply TlsIoClose(const std::string&) { return NotPortedYet("tls.io.close"); }

}  // namespace real
}  // namespace w2g
