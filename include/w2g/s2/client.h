#ifndef W2G_S2_CLIENT_H_
#define W2G_S2_CLIENT_H_

#include "w2g/s2/comms.h"

namespace w2g {
namespace s2 {

// Sandboxee-side channel. Connects using W2G_S2_COMMS.
class Client {
 public:
  Client();
  explicit Client(Comms* comms);

  Comms* comms() { return comms_; }
  bool SandboxMeHere();

 private:
  Comms owned_;
  Comms* comms_ = nullptr;
};

}  // namespace s2
}  // namespace w2g

#endif  // W2G_S2_CLIENT_H_
