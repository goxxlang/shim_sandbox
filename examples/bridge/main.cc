// wasi -> WasiNet -> Pipe()/Bus -> StubLayer -> W2gSapiHandle -> real OS.
// Same "wasigoc client, goclang++ server, one shim_sandbox process"
// shape a `goclang++.bat --shim-sandbox` build gets: the WASI-facing
// half (WasiNet) is what wasigoc-generated code would drive; the extra
// G++ half (StubLayer/W2gSapiHandle, src/sapi/real_win.cc) now does the
// real Winsock/Win32 work instead of always answering "not supported".
#include "w2g/stub.h"
#include "w2g/wasi.h"

#include <iostream>

namespace {

void Show(const char* label, const w2g::RecvResult& rec) {
  if (!rec.r1.is_nil()) {
    std::cout << label << ": recv error\n";
    return;
  }
  std::cout << label << ": " << rec.r0.from << " -> " << rec.r0.topic << "\n  "
            << w2g::ToString(rec.r0.payload) << "\n";
}

}  // namespace

int main() {
  w2g::Bus bus;
  w2g::WasiNet wasi(&bus);
  wasi.Attach();
  auto stubs = w2g::DefaultStubs(&bus);
  stubs->Install();
  stubs->GoServe();

  wasigo::go([&]() -> wasigo::Task {
    Show("net.dial     ", co_await wasi.Dial("tcp", "example.com:80"));
    Show("net.listen   ", co_await wasi.Listen("tcp", "127.0.0.1:0"));
    Show("net.tcp.bind ", co_await wasi.TcpBind("0.0.0.0:0"));
    Show("net.udp.bind ", co_await wasi.UdpBind("0.0.0.0:0"));
    Show("os.exec      ",
        co_await wasi.Exec({"cmd.exe", "/c", "echo", "hello", "from", "goclang++", "server"}));
    Show("os.user      ", co_await wasi.CurrentUser());
    Show("syscall      ", co_await wasi.Syscall("getpid"));
    Show("tls.dial     ", co_await wasi.TlsDial("example.com:443"));
    stubs->Close();
    co_return;
  }());
  wasigo::run();
  return 0;
}
