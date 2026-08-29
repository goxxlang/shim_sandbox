#ifndef W2G_STUB_H_
#define W2G_STUB_H_

#include "w2g/abac.h"
#include "w2g/layer.h"
#include "w2g/system_policy.h"

#include <memory>
#include <string>
#include <string_view>

namespace w2g {

// Extra G++ layers that stay stubbed (no sockets on wasm32-wasip1).
// They attach on Side::kGxx and speak Pipe pub/sub instead of Dial/Listen.
inline constexpr const char* kTopicDial = "net.dial";
inline constexpr const char* kTopicDialReply = "net.dial.reply";
inline constexpr const char* kTopicListen = "net.listen";
inline constexpr const char* kTopicListenReply = "net.listen.reply";
inline constexpr const char* kTopicTcpBind = "net.tcp.bind";
inline constexpr const char* kTopicTcpBindReply = "net.tcp.bind.reply";
inline constexpr const char* kTopicUdpBind = "net.udp.bind";
inline constexpr const char* kTopicUdpBindReply = "net.udp.bind.reply";

inline constexpr const char* kLayerDial = "gxx.dial";
inline constexpr const char* kLayerListen = "gxx.listen";
inline constexpr const char* kLayerTcp = "gxx.tcp";
inline constexpr const char* kLayerUdp = "gxx.udp";

inline constexpr const char* kNotSupported =
    "net: not supported on wasm32-wasip1 (WASI preview 1 has no socket syscalls)";

inline constexpr const char* kTopicExec = "os.exec";
inline constexpr const char* kTopicExecReply = "os.exec.reply";
inline constexpr const char* kTopicUser = "os.user";
inline constexpr const char* kTopicUserReply = "os.user.reply";
inline constexpr const char* kTopicSyscall = "syscall";
inline constexpr const char* kTopicSyscallReply = "syscall.reply";
inline constexpr const char* kTopicTlsDial = "tls.dial";
inline constexpr const char* kTopicTlsDialReply = "tls.dial.reply";

inline constexpr const char* kLayerExec = "gxx.exec";
inline constexpr const char* kLayerUser = "gxx.user";
inline constexpr const char* kLayerSyscall = "gxx.syscall";
inline constexpr const char* kLayerTls = "gxx.tls";

// One extra G++ stub: Handle() is ordinary C++, no coroutines.
class StubLayer : public ExtraLayer {
 public:
  StubLayer(const char* name, const char* request, const char* reply);
  const char* name() const override;
  const char* request_topic() const override;
  const char* reply_topic() const override;
  void Handle(const Msg& req, Msg* reply) override;
  void set_engine(abac::Engine* e) { engine_ = e; }

 private:
  const char* name_;
  const char* request_;
  const char* reply_;
  abac::Engine* engine_ = nullptr;
};

std::unique_ptr<ExtraLayer> MakeDialStub();
std::unique_ptr<ExtraLayer> MakeListenStub();
std::unique_ptr<ExtraLayer> MakeTcpStub();
std::unique_ptr<ExtraLayer> MakeUdpStub();
std::unique_ptr<ExtraLayer> MakeExecStub();
std::unique_ptr<ExtraLayer> MakeUserStub();
std::unique_ptr<ExtraLayer> MakeSyscallStub();
std::unique_ptr<ExtraLayer> MakeTlsStub();

// Four extra G++ layers, each on its own Pipe().
std::unique_ptr<Registry> DefaultStubs(Bus* bus);

// One combined extra layer subscribed to every stubbed net topic.
wasigo::Error AttachStubs(Bus* bus, std::string_view layer);
wasigo::Task ServeGxx(Bus* bus, std::string layer);

}  // namespace w2g

#endif  // W2G_STUB_H_
