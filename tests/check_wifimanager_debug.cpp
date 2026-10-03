#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>

struct HeapInfo { uint32_t total_free_bytes, largest_free_block; };
struct DebugPort {
  std::string output;
  void printf(const char* format, ...) __attribute__((format(printf,2,3))) {
    char bytes[128];
    va_list args;
    va_start(args,format);
    int count = std::vsnprintf(bytes, sizeof(bytes), format, args);
    va_end(args);
    assert(count > 0 && count < static_cast<int>(sizeof(bytes)));
    output = bytes;
  }
};

std::string diagnostic(HeapInfo info) {
  DebugPort _debugPort;
#include "wifimanager-debug-under-test.h"
  return _debugPort.output;
}

int main() {
  assert(diagnostic({253132,200000}) == "[MEM] free: 253132 | max: 200000 | frag:  21% \n");
  assert(diagnostic({200000,200000}) == "[MEM] free: 200000 | max: 200000 | frag:   0% \n");
  assert(diagnostic({0,0}) == "[MEM] free:     0 | max:     0 | frag:   0% \n");
  assert(diagnostic({1,0}) == "[MEM] free:     1 | max:     0 | frag: 100% \n");
  assert(diagnostic({UINT32_MAX,UINT32_MAX}) == "[MEM] free: 4294967295 | max: 4294967295 | frag:   0% \n");
  std::puts("PASS: actual WiFiManager heap diagnostics preserve blocks above 64 KiB, avoid division by zero/overflow and format matching unsigned types.");
}
