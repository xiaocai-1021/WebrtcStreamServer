#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
};

class MediaPacket {
 public:
  enum class Type : uint8_t { kVideo, kAudio };

  enum class CodecType { kH264, kOpus };

  using Pointer = std::shared_ptr<MediaPacket>;

  explicit MediaPacket(AVPacket* pkt);
  ~MediaPacket();

  uint8_t* Data() const;
  size_t Size() const;
  Type PacketType() const;
  void PacketType(enum Type type);
  int64_t TimestampMillis() const;
  bool IsKey() const;

 private:
  Type type_;
  AVPacket packet_;
};