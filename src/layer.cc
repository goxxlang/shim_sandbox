#include "w2g/layer.h"

namespace w2g {

wasigo::Error AttachLayer(Bus* bus, ExtraLayer* layer) {
  if (!bus || !layer) return wasigo::errors_new("w2g: nil layer");
  if (!bus->Find(layer->name())) {
    if (!bus->Attach(layer->name(), Side::kGxx)) {
      return wasigo::errors_new("w2g: attach failed");
    }
  }
  return bus->Subscribe(layer->name(), layer->request_topic());
}

wasigo::Task ServeLayer(Bus* bus, ExtraLayer* layer) {
  std::string name = layer->name();
  for (;;) {
    auto got = co_await bus->Recv(name);
    if (!got.r1.is_nil()) co_return;
    Msg reply;
    reply.from = name;
    reply.id = got.r0.id;
    reply.topic = layer->reply_topic();
    layer->Handle(got.r0, &reply);
    if (reply.topic.empty()) continue;
    auto err = co_await bus->Publish(reply);
    if (!err.is_nil()) co_return;
  }
}

Registry::Registry(Bus* bus) : bus_(bus) {}

ExtraLayer* Registry::Add(std::unique_ptr<ExtraLayer> layer) {
  ExtraLayer* raw = layer.get();
  extras_.push_back(std::move(layer));
  return raw;
}

wasigo::Error Registry::Install() {
  if (!bus_) return wasigo::errors_new("w2g: nil bus");
  for (auto& extra : extras_) {
    auto err = AttachLayer(bus_, extra.get());
    if (!err.is_nil()) return err;
  }
  return {};
}

void Registry::GoServe() {
  for (auto& extra : extras_) {
    ExtraLayer* layer = extra.get();
    wasigo::go(ServeLayer(bus_, layer));
  }
}

void Registry::Close() {
  if (!bus_) return;
  for (auto& extra : extras_) {
    bus_->CloseLayer(extra->name());
  }
}

}  // namespace w2g
