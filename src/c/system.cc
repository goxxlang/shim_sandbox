#include "w2g/c/system.h"

#include "w2g/bus.h"

#include <string>

struct W2gBus {
  w2g::Bus inner;
};

extern "C" {

void W2gInit(void) {}
void W2gShutdown(void) {}

W2gBus* W2gBusCreate(void) { return new W2gBus(); }

void W2gBusDestroy(W2gBus* bus) { delete bus; }

W2gResult W2gAttach(W2gBus* bus, const char* name, int side) {
  if (!bus || !name) return W2G_RESULT_INVALID_ARGUMENT;
  w2g::Side s = (side == W2G_SIDE_GXX) ? w2g::Side::kGxx : w2g::Side::kWasi;
  if (bus->inner.Find(name)) return W2G_RESULT_ALREADY_EXISTS;
  if (!bus->inner.Attach(name, s)) return W2G_RESULT_ALREADY_EXISTS;
  return W2G_RESULT_OK;
}

W2gResult W2gSubscribe(W2gBus* bus, const char* layer, const char* topic) {
  if (!bus || !layer || !topic) return W2G_RESULT_INVALID_ARGUMENT;
  auto err = bus->inner.Subscribe(layer, topic);
  if (err.is_nil()) return W2G_RESULT_OK;
  return W2G_RESULT_NOT_FOUND;
}

W2gResult W2gCloseLayer(W2gBus* bus, const char* name) {
  if (!bus || !name) return W2G_RESULT_INVALID_ARGUMENT;
  auto err = bus->inner.CloseLayer(name);
  if (err.is_nil()) return W2G_RESULT_OK;
  return W2G_RESULT_NOT_FOUND;
}

}
