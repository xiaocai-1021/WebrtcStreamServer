#include "random.h"

std::string Random::characters_{"0123456789abcdefghijklmnopqrstuvwxyz"};

Random::Random() : engine_{device_()} {}

uint32_t Random::RandomUInt(uint32_t min, uint32_t max) {
  std::uniform_int_distribution<> dist(min, max);
  return dist(engine_);
}

std::string Random::RandomString(size_t length) {
  std::string random_string;
  random_string.reserve(length);

  for (std::size_t i = 0; i < length; ++i) {
    random_string += characters_[RandomUInt(0, characters_.size() - 1)];
  }

  return random_string;
}