#pragma once

#include <random>
#include <string>

class Random {
 public:
  Random();
  uint32_t RandomUInt(uint32_t min, uint32_t max);
  std::string RandomString(size_t length);

 private:
  std::random_device device_;
  std::mt19937 engine_;
  static std::string characters_;
};