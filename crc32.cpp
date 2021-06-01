#include "crc32.h"

// This implementation is based on the sample implementation in RFC 1952.

// CRC32 polynomial, in reversed form.
// See RFC 1952, or http://en.wikipedia.org/wiki/Cyclic_redundancy_check
static const uint32_t kCrc32Polynomial = 0xEDB88320;

static uint32_t* LoadCrc32Table() {
  static uint32_t kCrc32Table[256];
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t c = i;
    for (size_t j = 0; j < 8; ++j) {
      if (c & 1) {
        c = kCrc32Polynomial ^ (c >> 1);
      } else {
        c >>= 1;
      }
    }
    kCrc32Table[i] = c;
  }
  return kCrc32Table;
}

const uint32_t* Crc32::crc32Table_ = LoadCrc32Table();

uint32_t Crc32::Calculate(const uint8_t* data, size_t size) {
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < size; ++i) {
    c = crc32Table_[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFF;
}