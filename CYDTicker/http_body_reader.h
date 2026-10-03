#pragma once
#include <cstddef>
#include <cstdint>

// ArduinoJson reader over an HTTP body. Decode chunk framing without retaining
// the response body in RAM. The underlying reader returns one byte, or -1.
template<class Source> class HttpBodyReader {
  Source& source;
  bool chunked, finished = false;
  size_t remaining = 0, total = 0;
  static constexpr size_t MAX_BODY = 256 * 1024;
  int raw() { return source.read(); }
public:
  HttpBodyReader(Source& s, bool chunks) : source(s), chunked(chunks) {}
  int read() {
    if (finished || total >= MAX_BODY) return -1;
    if (chunked && !remaining) {
      size_t size = 0; bool digit = false, extension = false;
      int c, length = 0;
      while ((c = raw()) >= 0 && c != '\r' && ++length <= 128) {
        if (c == ';') extension = true;
        if (extension) continue;
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0 || size > MAX_BODY / 16) { finished = true; return -1; }
        digit = true; size = size * 16 + d;
      }
      if (c != '\r' || raw() != '\n' || !digit || size > MAX_BODY - total || !size) { finished = true; return -1; }
      remaining = size;
    }
    int c = raw();
    if (c < 0) { finished = true; return -1; }
    ++total;
    if (chunked && !--remaining && (raw() != '\r' || raw() != '\n')) finished = true;
    return c;
  }
  size_t readBytes(char* out, size_t count) {
    size_t n = 0; int c;
    while (n < count && (c = read()) >= 0) out[n++] = (char)c;
    return n;
  }
};
