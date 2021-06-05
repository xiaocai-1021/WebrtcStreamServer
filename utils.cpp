#include "utils.h"

#include <chrono>
#include <iterator>
#include <boost/utility/string_view.hpp>
#include "spdlog/spdlog.h"
#include "spdlog/fmt/bin_to_hex.h"

int64_t TimeMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void DumpHex(const uint8_t* data, size_t size) {
  boost::string_view str((char*)data, size);
  spdlog::debug("{:n}", spdlog::to_hex(str));
}

NtpTime NtpTime::CreateFromMillis(uint64_t millis) {
  uint32_t seconds = millis / 1000;
  uint32_t fractions = static_cast<uint32_t>(
      (static_cast<double>(millis % 1000) / 1000) * kFractionsPerSecond);

  return NtpTime(seconds, fractions);
}

NtpTime NtpTime::CreateFromCompactNtp(uint32_t compact_ntp) {
  return NtpTime(compact_ntp >> 16, compact_ntp << 16);
}

NtpTime::NtpTime(uint32_t seconds, uint32_t fractions)
    : seconds_{seconds}, fractions_{fractions} {}

int64_t NtpTime::ToMillis() const {
  static constexpr double kNtpFracPerMs = 4.294967296E6;  // 2^32 / 1000.
  const double frac_ms = static_cast<double>(Fractions()) / kNtpFracPerMs;
  return 1000 * static_cast<int64_t>(Seconds()) +
          static_cast<int64_t>(frac_ms + 0.5);
}

uint32_t NtpTime::Seconds() const {
  return seconds_;
}

uint32_t NtpTime::Fractions() const {
  return fractions_;
}

uint32_t NtpTime::ToCompactNtp() {
  return (seconds_ << 16) | (fractions_ >> 16);
}

ScopeGuard::ScopeGuard(const std::function<void()>& f)
    : func_(f), dismiss_(false) {}

ScopeGuard::~ScopeGuard() {
  if (!dismiss_ && func_) func_();
}

void ScopeGuard::Dismiss() {
  dismiss_ = true;
}