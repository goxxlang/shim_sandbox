#ifndef W2G_BUS_H_
#define W2G_BUS_H_

#include "w2g/msg.h"
#include "w2g/pipe.h"

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace w2g {

enum class Side {
  kWasi,
  kGxx,
};

struct RecvResult {
  Msg r0{};
  wasigo::Error r1{};
};

// Pub/sub over Pipe(). Each attached layer gets one Pipe pair: the layer
// holds `self`, the bus holds `hub`. Publish writes one framed Msg on
// every subscriber's hub end except the sender. Recv reads one Msg from
// the layer's self end.
class Bus {
 public:
  struct Layer {
    std::string name;
    Side side = Side::kWasi;
    Conn* self = nullptr;
    Conn* hub = nullptr;
    std::vector<std::string> topics;
    bool closed = false;
  };

  Bus() = default;
  Bus(const Bus&) = delete;
  Bus& operator=(const Bus&) = delete;
  ~Bus();

  Layer* Attach(std::string name, Side side);
  Layer* Find(std::string_view name);
  const Layer* Find(std::string_view name) const;

  wasigo::Error Subscribe(std::string_view layer, std::string topic);
  wasigo::Error CloseLayer(std::string_view name);

  wasigo::TaskT<wasigo::Error> Publish(Msg msg);
  wasigo::TaskT<wasigo::Error> Publish(std::string from, std::string topic,
                                       wasigo::Slice<uint8_t> payload);
  wasigo::TaskT<RecvResult> Recv(std::string layer);
  // Publish, then Recv on `from` until a message with the same id arrives
  // on reply_topic. Extra G++ stubs copy req.id onto the reply.
  wasigo::TaskT<RecvResult> Call(std::string from, std::string topic,
                                 std::string reply_topic,
                                 wasigo::Slice<uint8_t> payload);

  const std::deque<Layer>& layers() const { return layers_; }

 private:
  bool Matches(const Layer& layer, std::string_view topic) const;

  std::deque<Layer> layers_;
  uint32_t next_id_ = 0;
};

}  // namespace w2g

#endif  // W2G_BUS_H_
