#pragma once

#include <cstdint>
#include <functional>

#include "spdlog/spdlog.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

extern "C" {
#include <opus/opus.h>
}

class OpusTranscoder {
 public:
  OpusTranscoder();
  ~OpusTranscoder();

  bool Open(AVCodecParameters* codecpar);
  bool Transcode(AVPacket* pkt);
  void RegisterTranscodeCallback(std::function<void(AVPacket* pkt)> cb);

 private:
  bool InitDecoder(AVCodecParameters* codecpar);
  bool InitResampler();
  bool InitEncoder();
  bool InitFifo();

  void ReleaseResources();

  AVCodecContext* decode_context_{nullptr};
  SwrContext* resample_context_{nullptr};
  AVFrame* decode_frame_{nullptr};
  AVAudioFifo* audio_fifo_{nullptr};
  OpusEncoder* opus_{nullptr};
  uint8_t* resample_output_buffer_[8];
  AVCodecParameters* decodec_codecpar_{nullptr};
  int resample_output_buffer_samples_per_channel_{0};
  int64_t audio_pts_ms_{0};
  std::function<void(AVPacket* pkt)> transcode_callback_;
};