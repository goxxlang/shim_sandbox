#include "test.h"

#include "w2g/c/system.h"

#include <cstdio>

int g_failures = 0;

int main() {
  W2gInit();
  int ran = 0;
  for (const auto& t : Tests()) {
    std::printf("RUN  %s\n", t.name);
    const int before = g_failures;
    t.fn();
    if (g_failures == before) {
      std::printf("OK   %s\n", t.name);
    } else {
      std::printf("FAIL %s\n", t.name);
    }
    ++ran;
  }
  W2gShutdown();
  std::printf("%d tests, %d failures\n", ran, g_failures);
  return g_failures ? 1 : 0;
}
