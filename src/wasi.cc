#include "w2g/wasi.h"

namespace w2g {

WasiNet::WasiNet(Bus* bus, std::string layer) : bus_(bus), layer_(std::move(layer)) {}

wasigo::Error WasiNet::Attach() {
  if (!bus_) return wasigo::errors_new("w2g: nil bus");
  if (!bus_->Find(layer_)) {
    if (!bus_->Attach(layer_, Side::kWasi)) {
      return wasigo::errors_new("w2g: attach failed");
    }
  }
  wasigo::Error err = bus_->Subscribe(layer_, kTopicDialReply);
  if (!err.is_nil()) return err;
  err = bus_->Subscribe(layer_, kTopicListenReply);
  if (!err.is_nil()) return err;
  err = bus_->Subscribe(layer_, kTopicTcpBindReply);
  if (!err.is_nil()) return err;
  err = bus_->Subscribe(layer_, kTopicUdpBindReply);
  if (!err.is_nil()) return err;
  err = bus_->Subscribe(layer_, kTopicExecReply);
  if (!err.is_nil()) return err;
  err = bus_->Subscribe(layer_, kTopicUserReply);
  if (!err.is_nil()) return err;
  err = bus_->Subscribe(layer_, kTopicSyscallReply);
  if (!err.is_nil()) return err;
  return bus_->Subscribe(layer_, kTopicTlsDialReply);
}

wasigo::TaskT<RecvResult> WasiNet::call(const char* topic, const char* reply,
                                        const std::string& payload) {
  co_return co_await bus_->Call(layer_, topic, reply, ToSlice(payload));
}

wasigo::TaskT<RecvResult> WasiNet::Dial(std::string network, std::string address) {
  co_return co_await call(kTopicDial, kTopicDialReply, network + " " + address);
}

wasigo::TaskT<RecvResult> WasiNet::Listen(std::string network, std::string address) {
  co_return co_await call(kTopicListen, kTopicListenReply, network + " " + address);
}

wasigo::TaskT<RecvResult> WasiNet::TcpBind(std::string address) {
  co_return co_await call(kTopicTcpBind, kTopicTcpBindReply, address);
}

wasigo::TaskT<RecvResult> WasiNet::UdpBind(std::string address) {
  co_return co_await call(kTopicUdpBind, kTopicUdpBindReply, address);
}

wasigo::TaskT<RecvResult> WasiNet::Exec(std::vector<std::string> argv) {
  std::string payload;
  for (size_t i = 0; i < argv.size(); ++i) {
    if (i) payload.push_back('\x1f');
    payload += argv[i];
  }
  co_return co_await call(kTopicExec, kTopicExecReply, payload);
}

wasigo::TaskT<RecvResult> WasiNet::CurrentUser() {
  co_return co_await call(kTopicUser, kTopicUserReply, "");
}

wasigo::TaskT<RecvResult> WasiNet::Syscall(std::string op, std::string arg) {
  co_return co_await call(kTopicSyscall, kTopicSyscallReply, arg.empty() ? op : op + " " + arg);
}

wasigo::TaskT<RecvResult> WasiNet::TlsDial(std::string address) {
  co_return co_await call(kTopicTlsDial, kTopicTlsDialReply, address);
}

}  // namespace w2g
