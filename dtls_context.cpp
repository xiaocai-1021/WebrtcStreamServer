#include "dtls_context.h"

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

#include "dtls_transport.h"
#include "spdlog/spdlog.h"

// Certificate default validity lifetime.
static const int kDefaultCertificateLifetimeInSeconds =
    60 * 60 * 24 * 30;  // 30 days
// Random bits for certificate serial number
static const int SERIAL_RAND_BITS = 64;
static const int kRsaDefaultModSize = 1024;
static const int kRsaDefaultExponent = 0x10001;  // = 2^16+1 = 65537

std::map<std::string, DtlsContext::Hash> DtlsContext::string_to_Hash_ = {
    {"sha-1", DtlsContext::Hash::kSha1},
    {"sha-224", DtlsContext::Hash::kSha224},
    {"sha-256", DtlsContext::Hash::kSha256},
    {"sha-384", DtlsContext::Hash::kSha384},
    {"sha-512", DtlsContext::Hash::kSha512}};

std::map<DtlsContext::Hash, std::string> DtlsContext::Hash_tp_string_ = {
    {DtlsContext::Hash::kSha1, "sha-1"},
    {DtlsContext::Hash::kSha224, "sha-224"},
    {DtlsContext::Hash::kSha256, "sha-256"},
    {DtlsContext::Hash::kSha384, "sha-384"},
    {DtlsContext::Hash::kSha512, "sha-512"}};

DtlsContext::Hash DtlsContext::GetHashFromString(const std::string& hash_name) {
  auto result = string_to_Hash_.find(hash_name);
  if (result != string_to_Hash_.end())
    return result->second;
  return DtlsContext::Hash::kUnknown;
}

std::string DtlsContext::GetStringFromHash(DtlsContext::Hash hash) {
  auto result = Hash_tp_string_.find(hash);
  if (result != Hash_tp_string_.end())
    return result->second;
  return "";
}

bool DtlsContext::IsDtls(uint8_t* data, size_t size) {
  return (size >= 13) && (data[0] > 19 && data[0] < 64);
}

bool DtlsContext::MakeRSAKeyPair() {
  private_key_ = EVP_PKEY_new();
  BIGNUM* exponent = BN_new();
  RSA* rsa = RSA_new();
  if (!private_key_ || !exponent || !rsa ||
      !BN_set_word(exponent, kRsaDefaultExponent) ||
      !RSA_generate_key_ex(rsa, kRsaDefaultModSize, exponent, nullptr) ||
      !EVP_PKEY_assign_RSA(private_key_, rsa)) {
    EVP_PKEY_free(private_key_);
    BN_free(exponent);
    RSA_free(rsa);
    spdlog::error("Failed to make RSA key pair.");
    return false;
  }
  // ownership of rsa struct was assigned, don't free it.
  BN_free(exponent);
  return true;
}

bool DtlsContext::MakeCertificate() {
  ASN1_INTEGER* asn1_serial_number = nullptr;
  BIGNUM* serial_number = nullptr;
  X509_NAME* name = nullptr;
  time_t epoch_off = 0;  // Time offset since epoch.

  if ((certificate_ = X509_new()) == nullptr) {
    goto error;
  }
  if (!X509_set_pubkey(certificate_, private_key_)) {
    goto error;
  }
  // serial number - temporary reference to serial number inside x509 struct
  if ((serial_number = BN_new()) == nullptr ||
      !BN_pseudo_rand(serial_number, SERIAL_RAND_BITS, 0, 0) ||
      (asn1_serial_number = X509_get_serialNumber(certificate_)) == nullptr ||
      !BN_to_ASN1_INTEGER(serial_number, asn1_serial_number)) {
    goto error;
  }
  // Set version to X509.V3
  if (!X509_set_version(certificate_, 2L)) {
    goto error;
  }

  if ((name = X509_NAME_new()) == nullptr || !X509_set_subject_name(certificate_, name) || !X509_set_issuer_name(certificate_, name)) {
    goto error;
  }

  if (!X509_time_adj(X509_get_notBefore(certificate_), -1 * kDefaultCertificateLifetimeInSeconds, &epoch_off) ||
      !X509_time_adj(X509_get_notAfter(certificate_), kDefaultCertificateLifetimeInSeconds, &epoch_off)) {
    goto error;
  }
  if (!X509_sign(certificate_, private_key_, EVP_sha256())) {
    goto error;
  }

  BN_free(serial_number);
  X509_NAME_free(name);
  return true;

error:
  BN_free(serial_number);
  X509_NAME_free(name);
  X509_free(certificate_);
  return false;
}

bool DtlsContext::Initialize() {
  ssl_ctx_ = SSL_CTX_new(DTLS_method());

  if (!ssl_ctx_)
    return false;
  SSL_CTX_set_min_proto_version(ssl_ctx_, DTLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx_, DTLS1_2_VERSION);
  SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
  SSL_CTX_set_cert_verify_callback(ssl_ctx_, [](X509_STORE_CTX* store, void* arg) {
    return 1;
  }, nullptr);
  SSL_CTX_set_info_callback(ssl_ctx_, [](const SSL* ssl, int where, int ret) {
    DtlsTransport* transport = (DtlsTransport*)SSL_get_ex_data(ssl, 0);
    transport->OnSSLInfo(where, ret);
  });

  if (!MakeRSAKeyPair() || !MakeCertificate()) {
    ReleaseResources();
    return false;
  }

  if (!SSL_CTX_use_certificate(ssl_ctx_, certificate_)) {
    spdlog::error("SSL_CTX_use_certificate failed.");
    ReleaseResources();
    return false;
  }

  if (!SSL_CTX_use_PrivateKey(ssl_ctx_, private_key_)) {
    spdlog::error("SSL_CTX_use_PrivateKey failed.");
    ReleaseResources();
    return false;
  }

  if (!SSL_CTX_set_cipher_list(ssl_ctx_, "DEFAULT:!NULL:!aNULL:!SHA256:!SHA384:!aECDH:!AESGCM+AES256:!aPSK")) {
    spdlog::error("SSL_CTX_set_cipher_list failed.");
    ReleaseResources();
    return false;
  }

  if (SSL_CTX_set_tlsext_use_srtp(ssl_ctx_,
                                  "SRTP_AES128_CM_SHA1_80:SRTP_AES128_CM_SHA1_32"
                                  ":SRTP_AEAD_AES_128_GCM:SRTP_AEAD_AES_256_GCM")) {
    ReleaseResources();
    return false;
  }
  
  DtlsContext::availableHashes_.push_back(kSha1);
  DtlsContext::availableHashes_.push_back(kSha224);
  DtlsContext::availableHashes_.push_back(kSha256);
  DtlsContext::availableHashes_.push_back(kSha384);
  DtlsContext::availableHashes_.push_back(kSha512);

  for (int i = 0; i < availableHashes_.size(); i++) {
    Hash hash = availableHashes_[i];
    unsigned int size;
    unsigned char fingerprint[EVP_MAX_MD_SIZE];
    char hex_fingerprint[EVP_MAX_MD_SIZE * 3 + 1] = {0};

    switch (hash) {
      case kSha1:
        X509_digest(certificate_, EVP_sha1(), fingerprint, &size);
        break;
      case kSha224:
        X509_digest(certificate_, EVP_sha224(), fingerprint, &size);
        break;
      case kSha256:
        X509_digest(certificate_, EVP_sha256(), fingerprint, &size);
        break;
      case kSha384:
        X509_digest(certificate_, EVP_sha384(), fingerprint, &size);
        break;
      case kSha512:
        X509_digest(certificate_, EVP_sha512(), fingerprint, &size);
        break;
      default: {
        spdlog::error("Unknown hash.",
                      hash);
        ReleaseResources();
        return false;
      }
    }

    if (!size) {
      spdlog::error("Wrong X509 certificate size.");
      ReleaseResources();
      return false;
    }

    for (int j = 0; j < size; j++)
      sprintf(hex_fingerprint + j * 3, "%.2X:", fingerprint[j]);

    hex_fingerprint[size * 3 - 1] = 0;

    DtlsContext::localFingerPrints_[hash] = std::string(hex_fingerprint);

    spdlog::debug("LocalFingerprint {} {}.", hash,
                  DtlsContext::localFingerPrints_[hash].c_str());
  }

  spdlog::debug("<DTLSConnection::Initialize({}).", availableHashes_.size());
  return true;
}

void DtlsContext::ReleaseResources() {
  if (private_key_) {
    EVP_PKEY_free(private_key_);
    private_key_ = nullptr;
  }
  if (certificate_) {
    X509_free(certificate_);
    certificate_ = nullptr;
  }
  if (ssl_ctx_) {
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
  }
}


DtlsContext::~DtlsContext() {
  ReleaseResources();
}

DtlsContext& DtlsContext::GetInstance() {
  static DtlsContext dtls_context;
  return dtls_context;
}

SSL_CTX* DtlsContext::GetDtlsContext() const {
  return ssl_ctx_;
}

std::string DtlsContext::GetCertificateFingerPrint(Hash hash) {
  return localFingerPrints_[hash];
}