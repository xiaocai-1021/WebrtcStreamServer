#pragma once

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/tls1.h>
#include <openssl/x509v3.h>

#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "random.h"

class DtlsContext {
 public:
  enum Hash { kSha1, kSha224, kSha256, kSha384, kSha512, kUnknown };
  ~DtlsContext();
  static DtlsContext& GetInstance();
  bool Initialize();
  SSL_CTX* GetDtlsContext() const;
  std::string GetCertificateFingerPrint(Hash hash);
  static DtlsContext::Hash GetHashFromString(const std::string& hash_name);
  static std::string GetStringFromHash(DtlsContext::Hash hash);
  static bool IsDtls(uint8_t* data, size_t size);

 private:
  using LocalFingerPrints = std::map<Hash, std::string>;
  using AvailableHashes = std::vector<Hash>;
  DtlsContext() = default;
  bool MakeRSAKeyPair();
  bool MakeCertificate();
  void ReleaseResources();
  SSL_CTX* ssl_ctx_{nullptr};
  X509* certificate_{nullptr};
  EVP_PKEY* private_key_{nullptr};
  LocalFingerPrints localFingerPrints_;
  AvailableHashes availableHashes_;
  Random random_;
  static std::map<std::string, DtlsContext::Hash> string_to_Hash_;
  static std::map<DtlsContext::Hash, std::string> Hash_tp_string_;
};