#pragma once

#include <boost/asio.hpp>
#include <set>
#include <string>
#include <cstddef>

#include "random.h"

using udp = boost::asio::ip::udp;

class IceLite {
 public:
  class Observer {
   public:
    virtual void OnStunMessageSend(uint8_t* data,
                                   size_t size,
                                   udp::endpoint* ep) = 0;
    virtual void OnIceConnectionCompleted() = 0;
    virtual void OnIceConnectionError() = 0;
  };

  IceLite(const std::string& remote_ufrag, Observer* observer);

  void ProcessStunMessage(uint8_t* data, size_t len, udp::endpoint* remote_ep);
  const std::string& GetLocalUfrag() const;
  const std::string& GetLocalPassword() const;
  const udp::endpoint* GetFavoredCandidate() const;

 private:
  Random random_;
  std::string local_ufrag_;
  std::string local_password_;
  std::string remote_ufrag_;
  Observer* observer_;
  std::set<udp::endpoint> valid_candidates_;
  udp::endpoint favored_candidate_;
  udp::endpoint old_favored_candidate_;
};