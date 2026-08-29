// WASI reactor. Linked --export-all --no-entry.
extern "C" {

void __wasm_call_ctors(void) __attribute__((weak));

__attribute__((export_name("_initialize"))) void _initialize(void) {
  if (__wasm_call_ctors) {
    __wasm_call_ctors();
  }
}

}
