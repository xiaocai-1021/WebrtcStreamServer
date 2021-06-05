#include "media_packet.h"

MediaPacket::MediaPacket(AVPacket* pkt) {
  av_packet_ref(&packet_, pkt);
}

MediaPacket::~MediaPacket() {
  av_packet_unref(&packet_);
}

uint8_t* MediaPacket::Data() const {
  return packet_.data;
}

size_t MediaPacket::Size() const {
  return packet_.size;
}

MediaPacket::Type MediaPacket::PacketType() const {
  return type_;
}

void MediaPacket::PacketType(enum Type type) {
  type_ = type;
}

int64_t MediaPacket::TimestampMillis() const {
  return packet_.pts;
}

bool MediaPacket::IsKey() const {
  return packet_.flags & AV_PKT_FLAG_KEY;
}