#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <functional>
#include <cstddef>

int64_t TimeMillis();

void DumpHex(const uint8_t* data, size_t size);

class NtpTime {
 public:
  static NtpTime CreateFromMillis(uint64_t millis);
  static NtpTime CreateFromCompactNtp(uint32_t compact_ntp);

  NtpTime(uint32_t seconds, uint32_t fractions);

  int64_t ToMillis() const;
  uint32_t Seconds() const;
  uint32_t Fractions() const;
  uint32_t ToCompactNtp();

 private:
  static constexpr uint64_t kFractionsPerSecond = 1ULL << 32;
  uint32_t seconds_;
  uint32_t fractions_;
};

class ScopeGuard {
 public:
  explicit ScopeGuard(const std::function<void()>& f);

  ~ScopeGuard();

  void Dismiss();

 private:
  std::function<void()> func_;
  bool dismiss_;
};