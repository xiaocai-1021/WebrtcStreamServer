#include "gop_cache.h"

void GopCache::AddPacket(MediaPacket::Pointer pkt) {
  if (!pkt)
    return;

  if (pkt->PacketType() == MediaPacket::Type::kVideo 
    && pkt->IsKey()) {
    cached_packets_.clear();
  }
  cached_packets_.push_back(pkt);
}

std::list<MediaPacket::Pointer> GopCache::GetCachedPackets() {
  return cached_packets_;
}