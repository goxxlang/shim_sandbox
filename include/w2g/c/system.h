#ifndef W2G_C_SYSTEM_H_
#define W2G_C_SYSTEM_H_

#include "w2g/c/types.h"

#ifdef __cplusplus
extern "C" {
#endif

void W2gInit(void);
void W2gShutdown(void);

typedef struct W2gBus W2gBus;

W2gBus* W2gBusCreate(void);
void W2gBusDestroy(W2gBus* bus);
W2gResult W2gAttach(W2gBus* bus, const char* name, int side);
W2gResult W2gSubscribe(W2gBus* bus, const char* layer, const char* topic);
W2gResult W2gCloseLayer(W2gBus* bus, const char* name);

#ifdef __cplusplus
}
#endif

#endif  // W2G_C_SYSTEM_H_
