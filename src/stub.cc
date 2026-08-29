#include "w2g/stub.h"

#include "w2g/channel.h"
#include "w2g/abac.h"
#include "w2g/system_policy.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace w2g {

namespace {

const char* CombinedReplyTopic(std::string_view topic) {
  if (topic == kTopicDial) return kTopicDialReply;
  if (topic == kTopicListen) return kTopicListenReply;
  if (topic == kTopicTcpBind) return kTopicTcpBindReply;
  if (topic == kTopicUdpBind) return kTopicUdpBindReply;
  if (topic == kTopicExec) return kTopicExecReply;
  if (topic == kTopicUser) return kTopicUserReply;
  if (topic == kTopicSyscall) return kTopicSyscallReply;
  if (topic == kTopicTlsDial) return kTopicTlsDialReply;
  return nullptr;
}

[[maybe_unused]] bool SystemTopic(std::string_view topic) {
  return topic == kTopicExec || topic == kTopicUser || topic == kTopicSyscall ||
         topic == kTopicTlsDial;
}

}  // namespace

StubLayer::StubLayer(const char* name, const char* request, const char* reply)
    : name_(name), request_(request), reply_(reply) {}

const char* StubLayer::name() const { return name_; }
const char* StubLayer::request_topic() const { return request_; }
const char* StubLayer::reply_topic() const { return reply_; }

void StubLayer::Handle(const Msg& req, Msg* reply) {
  if (!reply) return;
#if !W2G_ABAC_SYSTEM
  if (SystemTopic(req.topic)) {
    reply->from = name_;
    reply->topic = reply_;
    reply->id = req.id;
    reply->payload = ToSlice(std::string_view(kSystemDisabled));
    return;
  }
#endif
  if (engine_ && !engine_->Check(name_, req.topic, ToString(req.payload))) {
    reply->from = name_;
    reply->topic = reply_;
    reply->id = req.id;
    reply->payload = ToSlice(std::string_view("abac deny"));
    return;
  }
  char topic[W2G_SAPI_TOPIC_MAX] = {};
  std::vector<uint8_t> buf(4096);
  uint32_t n = static_cast<uint32_t>(buf.size());
  std::string payload = ToString(req.payload);
  W2gResult rc = DefaultChannel().Handle(
      req.topic.c_str(),
      reinterpret_cast<const uint8_t*>(payload.data()),
      static_cast<uint32_t>(payload.size()), topic, W2G_SAPI_TOPIC_MAX,
      buf.data(), &n);
  if (rc == W2G_RESULT_NOT_FOUND || rc == W2G_RESULT_INVALID_ARGUMENT) {
    reply->topic.clear();
    return;
  }
  reply->from = name_;
  reply->topic = topic[0] ? topic : reply_;
  reply->id = req.id;
  buf.resize(n);
  reply->payload = ToSlice(buf);
}

std::unique_ptr<ExtraLayer> MakeDialStub() {
  return std::make_unique<StubLayer>(kLayerDial, kTopicDial, kTopicDialReply);
}
std::unique_ptr<ExtraLayer> MakeListenStub() {
  return std::make_unique<StubLayer>(kLayerListen, kTopicListen, kTopicListenReply);
}
std::unique_ptr<ExtraLayer> MakeTcpStub() {
  return std::make_unique<StubLayer>(kLayerTcp, kTopicTcpBind, kTopicTcpBindReply);
}
std::unique_ptr<ExtraLayer> MakeUdpStub() {
  return std::make_unique<StubLayer>(kLayerUdp, kTopicUdpBind, kTopicUdpBindReply);
}
std::unique_ptr<ExtraLayer> MakeExecStub() {
  return std::make_unique<StubLayer>(kLayerExec, kTopicExec, kTopicExecReply);
}
std::unique_ptr<ExtraLayer> MakeUserStub() {
  return std::make_unique<StubLayer>(kLayerUser, kTopicUser, kTopicUserReply);
}
std::unique_ptr<ExtraLayer> MakeSyscallStub() {
  return std::make_unique<StubLayer>(kLayerSyscall, kTopicSyscall, kTopicSyscallReply);
}
std::unique_ptr<ExtraLayer> MakeTlsStub() {
  return std::make_unique<StubLayer>(kLayerTls, kTopicTlsDial, kTopicTlsDialReply);
}

std::unique_ptr<Registry> DefaultStubs(Bus* bus) {
  auto reg = std::make_unique<Registry>(bus);
  reg->Add(MakeDialStub());
  reg->Add(MakeListenStub());
  reg->Add(MakeTcpStub());
  reg->Add(MakeUdpStub());
  reg->Add(MakeExecStub());
  reg->Add(MakeUserStub());
  reg->Add(MakeSyscallStub());
  reg->Add(MakeTlsStub());
  return reg;
}

wasigo::Error AttachStubs(Bus* bus, std::string_view layer) {
  if (!bus) return wasigo::errors_new("w2g: nil bus");
  wasigo::Error err = bus->Subscribe(layer, kTopicDial);
  if (!err.is_nil()) return err;
  err = bus->Subscribe(layer, kTopicListen);
  if (!err.is_nil()) return err;
  err = bus->Subscribe(layer, kTopicTcpBind);
  if (!err.is_nil()) return err;
  err = bus->Subscribe(layer, kTopicUdpBind);
  if (!err.is_nil()) return err;
  err = bus->Subscribe(layer, kTopicExec);
  if (!err.is_nil()) return err;
  err = bus->Subscribe(layer, kTopicUser);
  if (!err.is_nil()) return err;
  err = bus->Subscribe(layer, kTopicSyscall);
  if (!err.is_nil()) return err;
  return bus->Subscribe(layer, kTopicTlsDial);
}

wasigo::Task ServeGxx(Bus* bus, std::string layer) {
  auto payload = ToSlice(std::string_view(kNotSupported));
  for (;;) {
    auto got = co_await bus->Recv(layer);
    if (!got.r1.is_nil()) co_return;
    const char* reply = CombinedReplyTopic(got.r0.topic);
    if (!reply) continue;
    Msg out;
    out.from = layer;
    out.topic = reply;
    out.id = got.r0.id;
    out.payload = payload;
    auto err = co_await bus->Publish(out);
    if (!err.is_nil()) co_return;
  }
}

}  // namespace w2g
