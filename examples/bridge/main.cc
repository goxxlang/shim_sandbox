#include "w2g/stub.h"
#include "w2g/wasi.h"

#include <iostream>

int main() {
  w2g::Bus bus;
  w2g::WasiNet wasi(&bus);
  wasi.Attach();
  auto stubs = w2g::DefaultStubs(&bus);
  stubs->Install();
  stubs->GoServe();

  wasigo::go([&]() -> wasigo::Task {
    std::cout << "wasi -> " << w2g::kTopicDial << " tcp example.com:80\n";
    auto rec = co_await wasi.Dial("tcp", "example.com:80");
    if (!rec.r1.is_nil()) {
      std::cout << "wasi recv error\n";
      co_return;
    }
    std::cout << rec.r0.from << " -> " << rec.r0.topic << " "
              << w2g::ToString(rec.r0.payload) << "\n";
    stubs->Close();
    co_return;
  }());
  wasigo::run();
  return 0;
}
