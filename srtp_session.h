#pragma once

#include <srtp2/srtp.h>

#include <map>
#include <string>

#include "spdlog/spdlog.h"

class LibSrtpInitializer {
 public:
  static LibSrtpInitializer& GetInstance() {
    static LibSrtpInitializer instance;
    return instance;
  }

  bool Initialize() {
    int err = srtp_init();
    return err == srtp_err_status_ok;
  }

 private:
  LibSrtpInitializer() = default;
  ~LibSrtpInitializer() {
    int err = srtp_shutdown();
    if (err != srtp_err_status_ok)
      spdlog::error("srtp_shutdown failed. err = {}.", err);
  }
};

class SrtpSession {
 public:
  enum class CipherSuite {
    SUITE_NONE,
    SUITE_AES_CM_128_HMAC_SHA1_80,
    SUITE_AES_CM_128_HMAC_SHA1_32,
    SUITE_AEAD_AES_128_GCM,
    SUITE_AEAD_AES_256_GCM
  };

  struct CipherSuiteKeySaltLength {
    CipherSuiteKeySaltLength() = default;
    CipherSuiteKeySaltLength(size_t key_length, size_t salt_length)
        : key_length_{key_length}, salt_length_{salt_length} {}
    size_t key_length_{0};
    size_t salt_length_{0};
  };

  static SrtpSession::CipherSuite GetCipherSuiteFromString(
      const std::string& str);
  static SrtpSession::CipherSuiteKeySaltLength GetSuiteKeySaltLength(
      SrtpSession::CipherSuite suite);

  SrtpSession();
  ~SrtpSession();

  bool Init(bool is_incoming, CipherSuite suite, uint8_t* key, size_t keyLen);

  // Encrypts/signs an individual RTP/RTCP packet, in-place.
  // If an HMAC is used, this will increase the packet size.
  bool ProtectRtp(void* data, int in_len, int max_len, int* out_len);
  bool ProtectRtcp(void* data, int in_len, int max_len, int* out_len);
  // Decrypts/verifies an invidiual RTP/RTCP packet.
  // If an HMAC is used, this will decrease the packet size.
  bool UnprotectRtp(void* data, int in_len, int* out_len);
  bool UnprotectRtcp(void* data, int in_len, int* out_len);

 private:
  static std::map<std::string, SrtpSession::CipherSuite> str_to_cipher_suite_;
  static std::map<SrtpSession::CipherSuite, CipherSuiteKeySaltLength>
      cipher_suites_key_salt_length_;
  srtp_ctx_t_* session_{nullptr};
  int rtp_auth_tag_len_{0};
  int rtcp_auth_tag_len_{0};
};