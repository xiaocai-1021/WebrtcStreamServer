#include "hmac_sha1.h"

#include <openssl/sha.h>

HmacSha1::HmacSha1() {
  hmac_ctx_.reset(HMAC_CTX_new());
}

uint8_t* HmacSha1::Calculate(boost::string_view key,
                             const uint8_t* data,
                             size_t len) {
  if (HMAC_Init_ex(hmac_ctx_.get(), key.data(), key.length(), EVP_sha1(),
                   nullptr) != 1)
    return nullptr;

  if (HMAC_Update(hmac_ctx_.get(), data, static_cast<int>(len)) != 1)
    return nullptr;

  uint32_t result_len;

  if (HMAC_Final(hmac_ctx_.get(), (uint8_t*)buffer_, &result_len) != 1)
    return nullptr;

  if (result_len != kSha1ResultLength)
    return nullptr;

  return buffer_;
}