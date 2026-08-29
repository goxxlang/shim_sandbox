#ifndef W2G_LAYER_H_
#define W2G_LAYER_H_

#include "w2g/bus.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace w2g {

// Extra G++ layer. Handle() is ordinary C++ — ServeLayer does the Pipe IO.
class ExtraLayer {
 public:
  virtual ~ExtraLayer() = default;
  virtual const char* name() const = 0;
  virtual const char* request_topic() const = 0;
  virtual const char* reply_topic() const = 0;
  virtual void Handle(const Msg& req, Msg* reply) = 0;
};

wasigo::Error AttachLayer(Bus* bus, ExtraLayer* layer);
wasigo::Task ServeLayer(Bus* bus, ExtraLayer* layer);

// Owns extra G++ layers, attaches each on its own Pipe(), and serves them.
class Registry {
 public:
  explicit Registry(Bus* bus);
  ExtraLayer* Add(std::unique_ptr<ExtraLayer> layer);
  wasigo::Error Install();
  void GoServe();
  void Close();
  Bus* bus() const { return bus_; }

 private:
  Bus* bus_;
  std::vector<std::unique_ptr<ExtraLayer>> extras_;
};

}  // namespace w2g

#endif  // W2G_LAYER_H_
