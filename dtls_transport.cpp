#include "dtls_transport.h"

#include <chrono>

#include "spdlog/spdlog.h"

static const uint32_t kDtlsMtu = 1350;

std::map<std::string, DtlsTransport::Setup> DtlsTransport::string_to_setup_ = {
    {"active", DtlsTransport::Setup::kActive},
    {"passive", DtlsTransport::Setup::kPassive},
    {"actpass", DtlsTransport::Setup::kActPass}};

std::map<DtlsTransport::Setup, std::string> DtlsTransport::setup_to_string_ = {
    {DtlsTransport::Setup::kActive, "active"},
    {DtlsTransport::Setup::kPassive, "passive"},
    {DtlsTransport::Setup::kActPass, "actpass"}};

DtlsTransport::DtlsTransport(boost::asio::io_context& io_context,
                             Observer* listener)
    : io_context_{io_context}, listener_{listener} {
  setup_ = Setup::kUnknown;
  ssl_ = nullptr;
  read_bio_ = nullptr;
  write_bio_ = nullptr;
  remote_hash_ = DtlsContext::Hash::kUnknown;
  memset(remote_fingerprint_, 0, EVP_MAX_MD_SIZE);
}

DtlsTransport::~DtlsTransport() {
  Stop();
}

std::string DtlsTransport::GetLocalSetup() const {
  return setup_to_string_.at(setup_);
}

bool DtlsTransport::Init() {
  if (!(ssl_ = SSL_new(DtlsContext::GetInstance().GetDtlsContext()))) {
    spdlog::error("SSL_new failed.");
    return false;
  }

  SSL_set_ex_data(ssl_, 0, this);

  if (!(read_bio_ = BIO_new(BIO_s_mem()))) {
    SSL_free(ssl_);
    ssl_ = nullptr;
    return false;
  }

  if (!(write_bio_ = BIO_new(BIO_s_mem()))) {
    SSL_free(ssl_);
    BIO_free(read_bio_);
    ssl_ = nullptr;
    read_bio_ = nullptr;
    return false;
  }

  SSL_set_bio(ssl_, read_bio_, write_bio_);
  SSL_set_read_ahead(ssl_, 1);

  SSL_set_mtu(ssl_, kDtlsMtu);
  BIO_ctrl(write_bio_, BIO_CTRL_DGRAM_SET_MTU, kDtlsMtu, NULL);

  return true;
}

void DtlsTransport::OnTimerTimeout() {
  if (!inited_)
    return;
  int ret = DTLSv1_handle_timeout(ssl_);
  if (ret < 0) {
    int ssl_err = SSL_get_error(ssl_, ret);
    if (ssl_err != SSL_ERROR_WANT_WRITE) {
      spdlog::error("DTLSv1_handle_timeout error");
      return;
    }
  }

  TrySendPendingData();
  SetTimeout();
}

void DtlsTransport::TrySendPendingData() {
  if (BIO_ctrl_pending(write_bio_)) {
    int read_sie = BIO_read(write_bio_, read_buffer_, kReadBufferSize);
    listener_->OnDtlsTransportSendData(read_buffer_, read_sie);
  }
}

void DtlsTransport::SetTimeout() {
  timeval timeout = {};
  if (timer_ && DTLSv1_get_timeout(ssl_, &timeout)) {
    int delay = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;
    timer_->AsyncWait(delay);
  }
}

void DtlsTransport::CheckPending() {
  if (BIO_ctrl_pending(write_bio_)) {
    int read_sie = BIO_read(write_bio_, read_buffer_, kReadBufferSize);
    listener_->OnDtlsTransportSendData(read_buffer_, read_sie);
  }

  SetTimeout();
}

void DtlsTransport::ProcessDataFromPeer(const uint8_t* buffer, size_t size) {
  if (!inited_) {
    spdlog::warn("SSL is not ready yet.");
  }

  int written = BIO_write(read_bio_, buffer, size);

  int read_len = SSL_read(ssl_, read_buffer_, kReadBufferSize);

  TrySendPendingData();
  SetTimeout();

  if (read_len < 0) {
    int err = SSL_get_error(ssl_, read_len);
    if (err != SSL_ERROR_WANT_READ) {
      spdlog::warn("SSL_read error, ssl error = {}", err);
    }
  }

  if (SSL_get_shutdown(ssl_) & SSL_RECEIVED_SHUTDOWN) {
    spdlog::warn("Close_notify was received.");
    SSL_clear(ssl_);
    listener_->OnDtlsTransportShutdown();
  }
}

bool DtlsTransport::Start(const std::string& setup_in_sdp) {
  auto result = string_to_setup_.find(setup_in_sdp);
  if (result == string_to_setup_.end()) {
    spdlog::error("Set unknown setup.");
    return false;
  }

  Setup remote_setup = string_to_setup_.at(setup_in_sdp);

  switch (remote_setup) {
    case kPassive:
      setup_ = kActive;
      break;
    case kActPass:
      setup_ = kActive;
      break;
    case kActive:
      setup_ = kPassive;
    default:
      return false;
  }

  if (!ssl_)
    return false;

  setup_ == kActive ? SSL_set_connect_state(ssl_) : SSL_set_accept_state(ssl_);

  inited_ = true;
  SSL_do_handshake(ssl_);

  timer_.reset(new Timer(io_context_, this));
  SetTimeout();
  return true;
}

void DtlsTransport::SetRemoteFingerprint(const std::string& hash,
                                         const char* fingerprint) {
  remote_hash_ = DtlsContext::GetHashFromString(hash);
  char* tmp = strdup(fingerprint);
  char* str = tmp;
  char* value;
  int pos = 0;

  while ((value = strsep(&str, ":")) && (pos != (EVP_MAX_MD_SIZE - 1)))
    sscanf(value, "%02x", (unsigned int*)&remote_fingerprint_[pos++]);

  free(tmp);
}

void DtlsTransport::Stop() {
  inited_ = false;

  if (timer_)
    timer_.reset();

  if (ssl_) {
    SSL_free(ssl_);
    ssl_ = NULL;
    read_bio_ = NULL;
    write_bio_ = NULL;
  }
}

bool DtlsTransport::CheckRemoteCertificate() {
  X509* certificate;
  const EVP_MD* hash_function;
  unsigned char fingerprint[EVP_MAX_MD_SIZE];
  unsigned int size = 0;

  certificate = SSL_get_peer_certificate(this->ssl_);

  if (!certificate) {
    spdlog::error("No certificate was provided by the peer");
    return false;
  }

  switch (remote_hash_) {
    case DtlsContext::kSha1:
      hash_function = EVP_sha1();
      break;
    case DtlsContext::kSha224:
      hash_function = EVP_sha224();
      break;
    case DtlsContext::kSha256:
      hash_function = EVP_sha256();
      break;
    case DtlsContext::kSha384:
      hash_function = EVP_sha384();
      break;
    case DtlsContext::kSha512:
      hash_function = EVP_sha512();
      break;
    default:
      X509_free(certificate);
      spdlog::error("Unknown remote hash.");
      return false;
  }

  if (X509_digest(certificate, hash_function, fingerprint, &size) == 0 ||
      memcmp(fingerprint, remote_fingerprint_, size)) {
    spdlog::error("The fingerprint in SDP does not match the certificate.");
    X509_free(certificate);
    return false;
  }

  X509_free(certificate);
  return true;
}

bool DtlsTransport::ExtractSrtpParams() {
  size_t max_length = 44;
  uint8_t material[max_length * 2];
  uint8_t local_master_key[max_length];
  uint8_t remote_master_key[max_length];
  uint8_t *local_key, *local_salt, *remote_key, *remote_salt;

  auto profile = SSL_get_selected_srtp_profile(ssl_);
  if (!profile) {
    spdlog::error("SSL_get_selected_srtp_profile failed.");
    return false;
  }

  auto suite = SrtpSession::GetCipherSuiteFromString(profile->name);

  if (suite == SrtpSession::CipherSuite::SUITE_NONE) {
    spdlog::error("Unknown negotiated SRTP suite.");
    return false;
  }

  if (!SSL_export_keying_material(ssl_, material, max_length * 2,
                                  "EXTRACTOR-dtls_srtp", 19, NULL, 0, 0)) {
    spdlog::error(
        "Unable to extract SRTP keying material from DTLS-SRTP negotiation on "
        "RTP instance .");
    return false;
  }

  auto keysalt = SrtpSession::GetSuiteKeySaltLength(suite);
  size_t total = keysalt.key_length_ + keysalt.salt_length_;

  if (setup_ == kActive) {
    local_key = material;
    remote_key = local_key + keysalt.key_length_;
    local_salt = remote_key + keysalt.key_length_;
    remote_salt = local_salt + keysalt.salt_length_;
  } else {
    remote_key = material;
    local_key = remote_key + keysalt.key_length_;
    remote_salt = local_key + keysalt.key_length_;
    local_salt = remote_salt + keysalt.salt_length_;
  }

  memcpy(local_master_key, local_key, keysalt.key_length_);
  memcpy(local_master_key + keysalt.key_length_, local_salt,
         keysalt.salt_length_);
  memcpy(remote_master_key, remote_key, keysalt.key_length_);
  memcpy(remote_master_key + keysalt.key_length_, remote_salt,
         keysalt.salt_length_);

  listener_->OnDtlsTransportSetup(suite, local_master_key, total,
                                  remote_master_key, total);
  return true;
}

int DtlsTransport::SetupSRTP() {
  if (!CheckRemoteCertificate())
    spdlog::error("Check remote certificate failed.");

  ExtractSrtpParams();
  return 1;
}

void DtlsTransport::OnSSLInfo(int where, int ret) {
  int w = where & -SSL_ST_MASK;
  const char* role;

  if ((w & SSL_ST_CONNECT) != 0)
    role = "client";
  else if ((w & SSL_ST_ACCEPT) != 0)
    role = "server";
  else
    role = "undefined";

  if ((where & SSL_CB_LOOP) != 0) {
    spdlog::debug("[role:{}, action:'{}']", role,
                  SSL_state_string_long(this->ssl_));
  } else if ((where & SSL_CB_ALERT) != 0) {
    const char* alertType;

    switch (*SSL_alert_type_string(ret)) {
      case 'W':
        alertType = "warning";
        break;

      case 'F':
        alertType = "fatal";
        break;

      default:
        alertType = "undefined";
    }

    if ((where & SSL_CB_READ) != 0) {
      spdlog::warn("dtls received DTLS {} alert: {}", alertType,
                   SSL_alert_desc_string_long(ret));
    } else if ((where & SSL_CB_WRITE) != 0) {
      spdlog::debug("sending DTLS {} alert: {}", alertType,
                    SSL_alert_desc_string_long(ret));
    } else {
      spdlog::debug("DTLS {} alert: {}", alertType,
                    SSL_alert_desc_string_long(ret));
    }
  } else if ((where & SSL_CB_EXIT) != 0) {
    if (ret == 0)
      spdlog::debug("[role:{}, failed:'{}']", role,
                    SSL_state_string_long(this->ssl_));
    else if (ret < 0)
      spdlog::debug("role: {}, waiting:'{}']", role,
                    SSL_state_string_long(this->ssl_));
  } else if ((where & SSL_CB_HANDSHAKE_START) != 0) {
    spdlog::debug("DTLS handshake start");
  } else if ((where & SSL_CB_HANDSHAKE_DONE) != 0) {
    spdlog::debug("DTLS handshake done");
    if (!SetupSRTP())
      listener_->OnDtlsTransportError();
  }

  CheckPending();
}