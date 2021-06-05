#pragma once

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>
#include <boost/utility/string_view.hpp>

#include "media_packet.h"
#include "opus_transcoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

class MediaSource {
 public:
  class Observer {
   public:
    virtual void OnMediaPacketGenerated(MediaPacket::Pointer packet) = 0;
    virtual void OnMediaSouceEnd() = 0;
  };

  bool Open(boost::string_view url);
  void Start();
  void Stop();

  void RegisterObserver(Observer* observer);
  void DeregisterObserver(Observer* observer);
  const std::string& Url() const;

 private:
  void ReadPacket();
  bool ParseAVCDecoderConfigurationRecord(uint8_t* data, int size);
  static int InterruptCB(void* opaque);
  bool IsIOTimeout();
  void UpdateIOTime();
  void StreamEnd();
  const static int64_t kDefaultIOTimeoutMillis = 10 * 1000; // 10s.
  AVFormatContext* stream_context_{nullptr};
  std::string url_;
  int video_index_{-1};
  int audio_index_{-1};
  int64_t last_io_time_{-1};
  std::string sps_;
  std::string pps_;
  std::mutex observers_mutex_;
  std::list<Observer*> observers_;
  bool is_first_packet_{true};
  bool is_first_audio_packet_{true};
  int64_t first_packet_timestamp_ms_{0};
  int64_t first_audio_packet_timestamp_ms_{0};
  std::thread work_thread_;
  std::atomic<bool> closed_{false};
  std::unique_ptr<OpusTranscoder> opus_transcoder_;
  AVBSFContext* bit_stream_filter_{nullptr};
};