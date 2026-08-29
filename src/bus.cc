#include "w2g/bus.h"

namespace w2g {

Bus::~Bus() {
  for (auto& layer : layers_) {
    if (layer.self && !layer.closed) {
      layer.self->Close();
    }
    if (layer.hub && !layer.closed) {
      layer.hub->Close();
    }
    layer.closed = true;
  }
}

static bool ValidName(std::string_view n, size_t maxn) {
  if (n.empty() || n.size() > maxn) return false;
  if (n.find('\0') != std::string_view::npos) return false;
  return true;
}

Bus::Layer* Bus::Attach(std::string name, Side side) {
  if (!ValidName(name, kMaxLayerName)) return nullptr;
  if (layers_.size() >= kMaxLayers) return nullptr;
  if (Find(name)) return nullptr;
  auto pipe = Pipe();
  Layer layer;
  layer.name = std::move(name);
  layer.side = side;
  layer.self = pipe.r0;
  layer.hub = pipe.r1;
  layers_.push_back(std::move(layer));
  return &layers_.back();
}

Bus::Layer* Bus::Find(std::string_view name) {
  for (auto& layer : layers_) {
    if (layer.name == name) return &layer;
  }
  return nullptr;
}

const Bus::Layer* Bus::Find(std::string_view name) const {
  for (const auto& layer : layers_) {
    if (layer.name == name) return &layer;
  }
  return nullptr;
}

wasigo::Error Bus::Subscribe(std::string_view layer, std::string topic) {
  if (!ValidName(topic, kMaxTopic) && topic != "*") {
    return wasigo::errors_new("w2g: bad topic");
  }
  Layer* found = Find(layer);
  if (!found) return wasigo::errors_new("w2g: no such layer");
  for (const auto& t : found->topics) {
    if (t == topic) return {};
  }
  found->topics.push_back(std::move(topic));
  return {};
}

wasigo::Error Bus::CloseLayer(std::string_view name) {
  Layer* found = Find(name);
  if (!found) return wasigo::errors_new("w2g: no such layer");
  if (found->closed) return {};
  found->closed = true;
  if (found->self) found->self->Close();
  if (found->hub) found->hub->Close();
  return {};
}

bool Bus::Matches(const Layer& layer, std::string_view topic) const {
  for (const auto& t : layer.topics) {
    if (t == "*" || t == topic) return true;
  }
  return false;
}

wasigo::TaskT<wasigo::Error> Bus::Publish(std::string from, std::string topic,
                                          wasigo::Slice<uint8_t> payload) {
  Msg msg;
  msg.from = std::move(from);
  msg.topic = std::move(topic);
  msg.payload = payload;
  co_return co_await Publish(std::move(msg));
}

wasigo::TaskT<wasigo::Error> Bus::Publish(Msg msg) {
  auto bytes = Encode(msg);
  if (bytes.empty() && (!msg.from.empty() || !msg.topic.empty() || msg.payload.len() > 0)) {
    co_return wasigo::errors_new("w2g: frame too large");
  }
  auto framed = ToSlice(bytes);

  std::vector<Conn*> targets;
  for (auto& layer : layers_) {
    if (layer.closed) continue;
    if (layer.name == msg.from) continue;
    if (!Matches(layer, msg.topic)) continue;
    targets.push_back(layer.hub);
  }
  for (Conn* conn : targets) {
    auto wr = co_await conn->Write(framed);
    if (!wr.r1.is_nil()) co_return wr.r1;
  }
  co_return {};
}

wasigo::TaskT<RecvResult> Bus::Recv(std::string layer) {
  Layer* found = Find(layer);
  if (!found) {
    co_return RecvResult{{}, wasigo::errors_new("w2g: no such layer")};
  }
  if (found->closed || !found->self) {
    co_return RecvResult{{}, errClosedPipe};
  }
  auto rr = co_await found->self->ReadMsg();
  if (!rr.r1.is_nil()) {
    co_return RecvResult{{}, rr.r1};
  }
  Msg msg;
  if (!Decode(rr.r0, &msg)) {
    co_return RecvResult{{}, wasigo::errors_new("w2g: bad frame")};
  }
  co_return RecvResult{std::move(msg), {}};
}

wasigo::TaskT<RecvResult> Bus::Call(std::string from, std::string topic,
                                    std::string reply_topic,
                                    wasigo::Slice<uint8_t> payload) {
  Msg out;
  out.from = from;
  out.topic = std::move(topic);
  out.id = ++next_id_;
  if (out.id == 0) out.id = ++next_id_;
  out.payload = payload;
  auto err = co_await Publish(out);
  if (!err.is_nil()) {
    co_return RecvResult{{}, err};
  }
  for (;;) {
    auto rec = co_await Recv(from);
    if (!rec.r1.is_nil()) co_return rec;
    if (rec.r0.id == out.id && rec.r0.topic == reply_topic) co_return rec;
  }
}

}  // namespace w2g
