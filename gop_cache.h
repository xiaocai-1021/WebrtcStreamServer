#pragma once

#include <list>

#include "media_packet.h"

class GopCache {
 public:
  void AddPacket(MediaPacket::Pointer pkt);
  std::list<MediaPacket::Pointer> GetCachedPackets();

 private:
  std::list<MediaPacket::Pointer> cached_packets_;
};