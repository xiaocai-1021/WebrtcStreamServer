#pragma once

#include <cstddef>
#include <cstdint>

class Crc32 {
 public:
  static uint32_t Calculate(const uint8_t* data, size_t size);

 private:
  static const uint32_t* crc32Table_;
};