#include "srtp_session.h"

std::map<std::string, SrtpSession::CipherSuite>
    SrtpSession::str_to_cipher_suite_ = {
        {"SRTP_AES128_CM_SHA1_80", CipherSuite::SUITE_AES_CM_128_HMAC_SHA1_80},
        {"SRTP_AES128_CM_SHA1_32", CipherSuite::SUITE_AES_CM_128_HMAC_SHA1_32},
        {"SRTP_AEAD_AES_128_GCM", CipherSuite::SUITE_AEAD_AES_128_GCM},
        {"SRTP_AEAD_AES_256_GCM", CipherSuite::SUITE_AEAD_AES_256_GCM}};

std::map<SrtpSession::CipherSuite, SrtpSession::CipherSuiteKeySaltLength>
    SrtpSession::cipher_suites_key_salt_length_ = {
        {SrtpSession::CipherSuite::SUITE_AES_CM_128_HMAC_SHA1_80, {16, 14}},
        {SrtpSession::CipherSuite::SUITE_AES_CM_128_HMAC_SHA1_32, {16, 14}},
        {SrtpSession::CipherSuite::SUITE_AEAD_AES_128_GCM, {16, 12}},
        {SrtpSession::CipherSuite::SUITE_AEAD_AES_256_GCM, {32, 12}},
};

SrtpSession::CipherSuite SrtpSession::GetCipherSuiteFromString(
    const std::string& str) {
  auto iter = str_to_cipher_suite_.find(str);
  if (iter == str_to_cipher_suite_.cend())
    return CipherSuite::SUITE_NONE;
  return iter->second;
}

SrtpSession::CipherSuiteKeySaltLength SrtpSession::GetSuiteKeySaltLength(
    SrtpSession::CipherSuite suite) {
  return cipher_suites_key_salt_length_[suite];
}

SrtpSession::SrtpSession() {}

SrtpSession::~SrtpSession() {
  if (session_) {
    srtp_set_user_data(session_, nullptr);
    srtp_dealloc(session_);
  }
}

bool SrtpSession::Init(bool is_incoming,
                       CipherSuite suite,
                       uint8_t* key,
                       size_t keyLen) {
  srtp_policy_t policy;
  memset(&policy, 0, sizeof(policy));

  switch (suite) {
    case CipherSuite::SUITE_AES_CM_128_HMAC_SHA1_80: {
      srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
      srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

      break;
    }

    case CipherSuite::SUITE_AES_CM_128_HMAC_SHA1_32: {
      srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy.rtp);
      srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

      break;
    }

    case CipherSuite::SUITE_AEAD_AES_128_GCM: {
      srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtp);
      srtp_crypto_policy_set_aes_gcm_128_16_auth(&policy.rtcp);

      break;
    }

    case CipherSuite::SUITE_AEAD_AES_256_GCM: {
      srtp_crypto_policy_set_aes_gcm_256_16_auth(&policy.rtp);
      srtp_crypto_policy_set_aes_gcm_256_16_auth(&policy.rtcp);

      break;
    }

    default:
      return false;
  }

  if (is_incoming)
    policy.ssrc.type = ssrc_any_inbound;
  else
    policy.ssrc.type = ssrc_any_outbound;

  policy.ssrc.value = 0;
  policy.key = key;
  policy.allow_repeat_tx = 1;
  policy.window_size = 1024;
  policy.next = nullptr;

  srtp_err_status_t err = srtp_create(&session_, &policy);

  if (err != srtp_err_status_ok) {
    spdlog::error("Failed to create session srtp.");
    return false;
  }

  rtp_auth_tag_len_ = policy.rtp.auth_tag_len;
  rtcp_auth_tag_len_ = policy.rtcp.auth_tag_len;

  return true;
}

bool SrtpSession::ProtectRtp(void* data,
                             int in_len,
                             int max_len,
                             int* out_len) {
  if (!session_) {
    spdlog::warn("Failed to protect rtp packet: no Session");
    return false;
  }

  int need_len = in_len + rtp_auth_tag_len_;  // NOLINT
  if (max_len < need_len) {
    spdlog::warn(
        "Failed to protect rtp packet: The buffer length \
            {} is less than the needed {}.",
        max_len, need_len);
    return false;
  }

  *out_len = in_len;
  int err = srtp_protect(session_, data, out_len);
  if (err != srtp_err_status_ok) {
    spdlog::warn("Failed to protect rtp packet. err = {}", err);
    return false;
  }
  return true;
}

bool SrtpSession::ProtectRtcp(void* data,
                              int in_len,
                              int max_len,
                              int* out_len) {
  if (!session_) {
    spdlog::warn("Failed to protect rtcp packet: no Session");
    return false;
  }

  int need_len = in_len + sizeof(uint32_t) + rtcp_auth_tag_len_;  // NOLINT
  if (max_len < need_len) {
    spdlog::warn(
        "Failed to protect rtcp packet: The buffer length \
        {} is less than the needed {}.",
        max_len, need_len);
    return false;
  }

  *out_len = in_len;
  int err = srtp_protect_rtcp(session_, data, out_len);
  if (err != srtp_err_status_ok) {
    spdlog::warn("Failed to protect rtcp packet.");
    return false;
  }
  return true;
}

bool SrtpSession::UnprotectRtp(void* data, int in_len, int* out_len) {
  if (!session_) {
    spdlog::warn("Failed to unprotect rtp packet: no Session");
    return false;
  }

  *out_len = in_len;
  int err = srtp_unprotect(session_, data, out_len);
  if (err != srtp_err_status_ok) {
    spdlog::warn("Failed to unprotect rtp packet.");
    return false;
  }
  return true;
}

bool SrtpSession::UnprotectRtcp(void* data, int in_len, int* out_len) {
  if (!session_) {
    spdlog::warn("Failed to unprotect rtcp packet: no Session");
    return false;
  }

  *out_len = in_len;
  int err = srtp_unprotect_rtcp(session_, data, out_len);
  if (err != srtp_err_status_ok) {
    spdlog::warn("Failed to unprotect rtcp packet. err = {}", err);
    return false;
  }
  return true;
}