#include "media_source.h"

#include "byte_buffer.h"
#include "utils.h"
#include "spdlog/spdlog.h"

bool MediaSource::IsIOTimeout() {
  int64_t io_time = TimeMillis() - last_io_time_;
  return !closed_ ? io_time > kDefaultIOTimeoutMillis : true;
}

void MediaSource::UpdateIOTime() {
  last_io_time_ = TimeMillis();
}

int MediaSource::InterruptCB(void* opaque) {
  if (opaque) {
    bool timeout = static_cast<MediaSource*>(opaque)->IsIOTimeout();
    if (timeout)
      spdlog::debug("Detect io timeout.");
    return timeout;
  } 
  else {
    return false;
  }
}

bool MediaSource::Open(boost::string_view url) {
  int ret = -1;
  if (!url.starts_with("rtmp://"))
    return false;

  ScopeGuard guard([this] {
    Stop();
  });

  stream_context_ = avformat_alloc_context();
  if (!stream_context_)
    return false;
  stream_context_->interrupt_callback.callback = &MediaSource::InterruptCB;
  stream_context_->interrupt_callback.opaque = this;
  UpdateIOTime();
  ret = avformat_open_input(&stream_context_, url.data(), nullptr, nullptr);

  if (ret < 0) {
    spdlog::error("Open address {} fail.", url.data());
    return false;
  }

  UpdateIOTime();
  ret = avformat_find_stream_info(stream_context_, nullptr);

  if (ret < 0) {
    spdlog::error("Failed to find stream information.");
    return false;
  }

  for (uint32_t i = 0; i < stream_context_->nb_streams; ++i) {
    AVCodecParameters* codec_parameters = stream_context_->streams[i]->codecpar;
    if (codec_parameters->codec_type == AVMEDIA_TYPE_VIDEO) {
      if (codec_parameters->codec_id != AV_CODEC_ID_H264) {
        spdlog::error("Only H264 codec is supported.");
        return false;
      }
      video_index_ = i;
    }
    if (codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_index_ = i;
    }
  }

  if (video_index_ == -1 || audio_index_ == -1) {
    spdlog::error("Now only streams with audio and video are supported.");
    return false;
  }

  if (av_bsf_list_parse_str("h264_mp4toannexb,dump_extra=freq=keyframe", &bit_stream_filter_) < 0)
    return false;

  if (avcodec_parameters_copy(bit_stream_filter_->par_in
    , stream_context_->streams[video_index_]->codecpar) < 0)
    return false;
  if (av_bsf_init(bit_stream_filter_) < 0)
    return false;

  url_ = url.data();
  guard.Dismiss();
  return true;
}

const std::string& MediaSource::Url() const {
  return url_;
}

void MediaSource::RegisterObserver(Observer* observer) {
  std::lock_guard<std::mutex> guard(observers_mutex_);
  auto result = std::find(observers_.begin(), observers_.end(), observer);
  if (result != observers_.end())
    return;
  observers_.push_back(observer);
}

void MediaSource::DeregisterObserver(Observer* observer) {
  std::lock_guard<std::mutex> guard(observers_mutex_);
  auto result = std::find(observers_.begin(), observers_.end(), observer);
  if (result == observers_.end())
    return;
  observers_.erase(result);
}

void MediaSource::Stop() {
  closed_ = true;
  if (work_thread_.joinable())
    work_thread_.join();

  if (stream_context_)
    avformat_close_input(&stream_context_);
  if (bit_stream_filter_)
    av_bsf_free(&bit_stream_filter_);
}

void MediaSource::Start() {
  work_thread_ = std::thread(&MediaSource::ReadPacket, this);
}

void MediaSource::StreamEnd() {
  std::lock_guard<std::mutex> guard(observers_mutex_);
  for (auto observer : observers_)
    observer->OnMediaSouceEnd();
}

void MediaSource::ReadPacket() {
  AVPacket packet;

  while (!closed_) {
    UpdateIOTime();
    if (av_read_frame(stream_context_, &packet) < 0) {
      StreamEnd();
      return;
    }

    if (packet.stream_index == video_index_) {
      if (bit_stream_filter_) {
        if (av_bsf_send_packet(bit_stream_filter_, &packet) < 0) {
          StreamEnd();
          return;
        }

        if (av_bsf_receive_packet(bit_stream_filter_, &packet) < 0) {
          StreamEnd();
          return;
        }
      }

      auto p = std::make_shared<MediaPacket>(&packet);
      p->PacketType(MediaPacket::Type::kVideo);
      std::lock_guard<std::mutex> guard(observers_mutex_);
      for (auto observer : observers_)
        observer->OnMediaPacketGenerated(p);
    } else if (packet.stream_index == audio_index_) {
      if (is_first_audio_packet_) {
        first_audio_packet_timestamp_ms_ = packet.pts;
        is_first_audio_packet_ = false;
      }
      if (!opus_transcoder_) {
        opus_transcoder_.reset(new OpusTranscoder);
        opus_transcoder_->Open(
            stream_context_->streams[audio_index_]->codecpar);
        opus_transcoder_->RegisterTranscodeCallback([&](AVPacket* pkt) {
          if (pkt) {
            pkt->pts += first_audio_packet_timestamp_ms_;
            pkt->dts = pkt->pts;
          }
          auto p = std::make_shared<MediaPacket>(pkt);
          p->PacketType(MediaPacket::Type::kAudio);
          std::lock_guard<std::mutex> guard(observers_mutex_);
          for (auto observer : observers_)
            observer->OnMediaPacketGenerated(p);
        });
      }
      opus_transcoder_->Transcode(&packet);
    }
    av_packet_unref(&packet);
  }
}