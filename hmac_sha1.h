#pragma once

#include <openssl/hmac.h>

#include <cstdint>
#include <memory>
#include <string>
#include <boost/utility/string_view.hpp>

struct HmacDeleter {
  void operator()(HMAC_CTX* ctx) {
    HMAC_CTX_free(ctx);
  }
};

class HmacSha1 {
 public:
  static const uint32_t kSha1ResultLength = 20;
  HmacSha1();
  uint8_t* Calculate(boost::string_view key, const uint8_t* data, size_t len);

 private:
  uint8_t buffer_[kSha1ResultLength];
  std::unique_ptr<HMAC_CTX, HmacDeleter> hmac_ctx_;
};