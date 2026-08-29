#include "w2g/s2/client.h"

int main() {
  w2g::s2::Client client;
  if (!client.comms() || !client.comms()->IsConnected()) return 2;
  uint32_t n = 0;
  if (!client.comms()->RecvU32(&n)) return 3;
  if (!client.comms()->SendU32(n + 1)) return 4;
  return 0;
}
