#ifndef W2G_WASI_H_
#define W2G_WASI_H_

#include "w2g/stub.h"

#include <string>
#include <vector>

namespace w2g {

// WASI-facing side of the common interface. Dial/Listen/bind publish on
// Pipe() and wait for the extra G++ stub reply instead of returning a
// local "not supported" without talking to anyone.
class WasiNet {
 public:
  explicit WasiNet(Bus* bus, std::string layer = "wasi");

  wasigo::Error Attach();

  wasigo::TaskT<RecvResult> Dial(std::string network, std::string address);
  wasigo::TaskT<RecvResult> Listen(std::string network, std::string address);
  wasigo::TaskT<RecvResult> TcpBind(std::string address);
  wasigo::TaskT<RecvResult> UdpBind(std::string address);

  // Extra G++ system stubs (os.exec / os.user / syscall / tls.dial).
  wasigo::TaskT<RecvResult> Exec(std::vector<std::string> argv);
  wasigo::TaskT<RecvResult> CurrentUser();
  wasigo::TaskT<RecvResult> Syscall(std::string op, std::string arg = "");
  wasigo::TaskT<RecvResult> TlsDial(std::string address);

  const std::string& layer() const { return layer_; }

 private:
  wasigo::TaskT<RecvResult> call(const char* topic, const char* reply,
                                 const std::string& payload);

  Bus* bus_;
  std::string layer_;
};

}  // namespace w2g

#endif  // W2G_WASI_H_
