#include "opus_transcoder.h"

static const uint32_t kOutSampleRate = 48000;
static const uint32_t kOutChannels = 2;
static constexpr uint32_t kFrameSize = 960; // 20ms
static const uint32_t kSampleSize = 2; // pcm16le
static const uint32_t kOpusDefaultComplexity = 10; // range 1~10

OpusTranscoder::OpusTranscoder() {
  for (int i = 0; i < 8; ++i)
    resample_output_buffer_[i] = nullptr;
}

OpusTranscoder::~OpusTranscoder() {
  ReleaseResources();
}

bool OpusTranscoder::Open(AVCodecParameters* codecpar) {
  if (!InitDecoder(codecpar))
    return false;
  if (!InitEncoder())
    return false;
  if (!InitResampler())
    return false;
  if (!InitFifo())
    return false;
  return true;
}

bool OpusTranscoder::Transcode(AVPacket* pkt) {
  int ret = -1;
  if (!pkt)
    return false;
  ret = avcodec_send_packet(decode_context_, pkt);
  if (ret < 0) {
    spdlog::error("Error submitting the packet to the decoder");
    return false;
  }

  ret = avcodec_receive_frame(decode_context_, decode_frame_);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return true;
  } else if (ret < 0) {
    spdlog::error("Error during decoding");
    return false;
  }

  int64_t delay =
      swr_get_delay(resample_context_, decodec_codecpar_->sample_rate);
  int estimated =
      (int)av_rescale_rnd(delay + decode_frame_->nb_samples, kOutSampleRate,
                          decodec_codecpar_->sample_rate, AV_ROUND_UP);

  if (estimated > resample_output_buffer_samples_per_channel_) {
    if (resample_output_buffer_[0])
      av_freep(&resample_output_buffer_[0]);

    if (av_samples_alloc(resample_output_buffer_, NULL, kOutChannels, estimated,
                         AV_SAMPLE_FMT_S16, 0) < 0) {
      spdlog::error("av_samples_alloc failed.");
      return false;
    }

    resample_output_buffer_samples_per_channel_ = estimated;
  }

  ret = swr_convert(resample_context_, resample_output_buffer_,
                    resample_output_buffer_samples_per_channel_,
                    (const uint8_t**)decode_frame_->data,
                    decode_frame_->nb_samples);

  if (ret < 0) {
    spdlog::error("swr_convert failed.");
    return false;
  }

  av_frame_unref(decode_frame_);

  if (av_audio_fifo_write(audio_fifo_, (void**)resample_output_buffer_, ret) <
      ret) {
    spdlog::error("Could not write data to FIFO");
    return false;
  }

  uint8_t input_buffer[kFrameSize * kSampleSize * kOutChannels];
  uint8_t* input[1];
  input[0] = input_buffer;
  uint8_t outpu_buffer[kFrameSize * kSampleSize * kOutChannels];

  while (av_audio_fifo_size(audio_fifo_) >= kFrameSize) {
    int ret;
    int32_t n;

    n = av_audio_fifo_read(audio_fifo_, reinterpret_cast<void**>(input),
                           kFrameSize);
    if (n != kFrameSize) {
      spdlog::error("Cannot read enough data from fifo, needed {}, read {}",
                    kFrameSize, n);
      return false;
    }

    auto encoded_size =
        opus_encode(opus_, (opus_int16*)input_buffer, kFrameSize, outpu_buffer,
                    kFrameSize * kSampleSize * kOutChannels);
    if (encoded_size < 0) {
      spdlog::error("Error opus encodo.");
      return false;
    }

    AVPacket encoded_packet;
    if (av_new_packet(&encoded_packet, encoded_size) != 0)
      return false;
    memcpy(encoded_packet.data, outpu_buffer, encoded_size);
    encoded_packet.pts = encoded_packet.dts = audio_pts_ms_;
    audio_pts_ms_ += 20;
    if (transcode_callback_)
      transcode_callback_(&encoded_packet);
    av_packet_unref(&encoded_packet);
  }
  return true;
}

bool OpusTranscoder::InitFifo() {
  audio_fifo_ = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, kOutChannels, 1);
  if (!audio_fifo_) {
    spdlog::error("Could not allocate FIFO");
    return false;
  }
  return true;
}

bool OpusTranscoder::InitDecoder(AVCodecParameters* codecpar) {
  AVCodec* input_codec = nullptr;
  int error = -1;
  if (!codecpar)
    return false;
  decodec_codecpar_ = codecpar;
  if (!(input_codec = avcodec_find_decoder(codecpar->codec_id))) {
    spdlog::error("Could not find aac codec");
    return false;
  }

  decode_context_ = avcodec_alloc_context3(input_codec);
  if (!decode_context_) {
    spdlog::error("Could not allocate aac decoding context");
    return false;
  }

  if (avcodec_parameters_to_context(decode_context_, codecpar) < 0)
    return false;

  if ((error = avcodec_open2(decode_context_, input_codec, nullptr)) < 0) {
    spdlog::error("Could not open input codec.");
    ReleaseResources();
    return false;
  }

  decode_frame_ = av_frame_alloc();

  if (!decode_frame_) {
    ReleaseResources();
    return false;
  }

  return true;
}

bool OpusTranscoder::InitResampler() {
  resample_context_ = swr_alloc_set_opts(
      NULL, av_get_default_channel_layout(kOutChannels), AV_SAMPLE_FMT_S16,
      kOutSampleRate, av_get_default_channel_layout(decode_context_->channels),
      decode_context_->sample_fmt, decode_context_->sample_rate, 0, NULL);
  if (!resample_context_) {
    spdlog::error("Could not allocate resample context");
    return false;
  }

  if (swr_init(resample_context_) < 0) {
    spdlog::error("Could not open resample context");
    ReleaseResources();
    return false;
  }
  return true;
}

void OpusTranscoder::RegisterTranscodeCallback(
    std::function<void(AVPacket* pkt)> cb) {
  transcode_callback_ = cb;
}

bool OpusTranscoder::InitEncoder() {
  int error = 0;
  opus_ = opus_encoder_create(kOutSampleRate, 2, OPUS_APPLICATION_VOIP, &error);
  if (error != OPUS_OK) {
    spdlog::error("Error create Opus encoder");
    return false;
  }

  opus_encoder_ctl(opus_, OPUS_SET_COMPLEXITY(kOpusDefaultComplexity));
  opus_encoder_ctl(opus_, OPUS_SET_INBAND_FEC(1));
  return true;
}

void OpusTranscoder::ReleaseResources() {
  if (decode_context_)
    avcodec_free_context(&decode_context_);
  if (resample_context_)
    swr_free(&resample_context_);
  if (audio_fifo_) {
    av_audio_fifo_free(audio_fifo_);
    audio_fifo_ = nullptr;
  }

  if (opus_)
    opus_encoder_destroy(opus_);

  if (decode_frame_)
    av_frame_free(&decode_frame_);
  if (resample_output_buffer_[0])
    av_freep(&resample_output_buffer_[0]);
}