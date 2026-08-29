#include "w2g/s2/client.h"

#include "os.h"

namespace w2g {
namespace s2 {

Client::Client() {
  Handle r, w;
  if (os::CommsFromEnv(&r, &w)) {
    owned_ = Comms(std::move(r), std::move(w));
    comms_ = &owned_;
  }
}

Client::Client(Comms* comms) : comms_(comms) {}

bool Client::SandboxMeHere() {
  if (!comms_ || !comms_->IsConnected()) return false;
  return comms_->SendBool(true);
}

}  // namespace s2
}  // namespace w2g
