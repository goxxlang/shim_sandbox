#include "w2g/pipe.h"

namespace w2g {

wasigo::Error errNotSupported = wasigo::errors_new(
    "net: not supported on wasm32-wasip1 (WASI preview 1 has no socket syscalls)");
wasigo::Error errClosedPipe = wasigo::errors_new("net: pipe closed");
wasigo::Error errEOF = wasigo::errors_new("EOF");

}  // namespace w2g
