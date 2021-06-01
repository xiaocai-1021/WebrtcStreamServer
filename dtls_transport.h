#pragma once


#include <cstddef>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/opensslconf.h>
#include <openssl/ssl.h>

#include <atomic>
#include <boost/asio.hpp>
#include <map>
#include <memory>
#include <string>

#include "dtls_context.h"
#include "srtp_session.h"
#include "timer.h"

class DtlsTransport : public Timer::Listener {
 public:
  enum Setup { kActive, kPassive, kActPass, kUnknown };

  class Observer {
   public:
    virtual ~Observer() = default;

    virtual void OnDtlsTransportSetup(SrtpSession::CipherSuite suite,
                                      uint8_t* localMasterKey,
                                      int localMasterKeySize,
                                      uint8_t* remoteMasterKey,
                                      int remoteMasterKeySize) = 0;
    virtual void OnDtlsTransportError() = 0;
    virtual void OnDtlsTransportShutdown() = 0;
    virtual void OnDtlsTransportSendData(const uint8_t* data, size_t len) = 0;
  };

  DtlsTransport(boost::asio::io_context& io_context, Observer* listener);
  ~DtlsTransport();

  bool Init();
  void ProcessDataFromPeer(const uint8_t* buffer, size_t size);
  std::string GetLocalSetup() const;
  void SetRemoteFingerprint(const std::string& hash, const char* fingerprint);

  bool Start(const std::string& setup_in_sdp);
  void Stop();

 public:
  int SetupSRTP();
  void OnSSLInfo(int where, int ret);

 private:
  void OnTimerTimeout() override;
  void CheckPending();
  void SetTimeout();
  void TrySendPendingData();
  bool CheckRemoteCertificate();
  bool ExtractSrtpParams();

  boost::asio::io_context& io_context_;
  SSL* ssl_;
  BIO* read_bio_;
  BIO* write_bio_;
  Setup setup_;
  unsigned char remote_fingerprint_[EVP_MAX_MD_SIZE];
  DtlsContext::Hash remote_hash_;
  std::atomic<bool> inited_;
  std::unique_ptr<Timer> timer_;
  Observer* listener_;
  static constexpr int kReadBufferSize{65536};
  uint8_t read_buffer_[kReadBufferSize];
  static std::map<std::string, DtlsTransport::Setup> string_to_setup_;
  static std::map<DtlsTransport::Setup, std::string> setup_to_string_;
};