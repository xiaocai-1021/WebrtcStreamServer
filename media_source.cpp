#include "media_source.h"

#include "byte_buffer.h"
#include "spdlog/spdlog.h"

bool MediaSource::Open(boost::string_view url) {
  int ret = -1;
  ret = avformat_open_input(&stream_context_, url.data(), nullptr, nullptr);

  if (ret < 0) {
    spdlog::error("Open address {} fail.", url.data());
    return false;
  }

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
      uint8_t* data = stream_context_->streams[i]->codecpar->extradata;
      int size = stream_context_->streams[i]->codecpar->extradata_size;
      if (!ParseAVCDecoderConfigurationRecord(data, size)) {
        spdlog::error("Failed to parse information extradata");
        return false;
      }
    }
    if (codec_parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
      audio_index_ = i;
    }
  }

  url_ = url.data();

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

bool MediaSource::ParseAVCDecoderConfigurationRecord(uint8_t* data, int size) {
  uint8_t version;
  uint8_t profile_indication;
  uint8_t profile_compatibility;
  uint8_t avc_level;
  uint8_t length_size;

  std::vector<std::string> sps_list;
  std::vector<std::string> pps_list;

  ByteReader reader(data, size);

  if (!(reader.ReadUInt8(&version) && version == 1 &&
        reader.ReadUInt8(&profile_indication) &&
        reader.ReadUInt8(&profile_compatibility) &&
        reader.ReadUInt8(&avc_level))) {
    return false;
  }

  uint8_t length_size_minus_one;
  if (!reader.ReadUInt8(&length_size_minus_one))
    return false;
  length_size = (length_size_minus_one & 0x3) + 1;

  if (length_size == 3)  // Only values of 1, 2, and 4 are valid.
    return false;

  uint8_t num_sps;
  if (!reader.ReadUInt8(&num_sps))
    return false;
  num_sps &= 0x1f;

  for (int i = 0; i < num_sps; i++) {
    uint16_t sps_length;
    std::string sps;

    if (!reader.ReadUInt16(&sps_length))
      return false;
    if (!reader.ReadString(&sps, sps_length))
      return false;
    sps_list.push_back(sps);
  }

  uint8_t num_pps;
  if (!reader.ReadUInt8(&num_pps))
    return false;
  for (int i = 0; i < num_pps; i++) {
    uint16_t pps_length;
    std::string pps;

    if (!reader.ReadUInt16(&pps_length))
      return false;
    if (!reader.ReadString(&pps, pps_length))
      return false;
    pps_list.push_back(pps);
  }

  if (!sps_list.empty())
    sps_ = sps_list[0];
  if (!pps_list.empty())
    pps_ = pps_list[0];

  return true;
}

void MediaSource::Stop() {
  closed_ = true;
  if (work_thread_.joinable())
    work_thread_.join();

  if (stream_context_)
    avformat_close_input(&stream_context_);
}

void MediaSource::Start() {
  work_thread_ = std::thread(&MediaSource::ReadPacket, this);
}

void MediaSource::ReadPacket() {
  AVPacket packet;

  while (!closed_) {
    if (av_read_frame(stream_context_, &packet) < 0) {
      std::lock_guard<std::mutex> guard(observers_mutex_);
      for (auto observer : observers_)
        observer->OnMediaSouceEnd();
      return;
    }

    if (is_first_packet_) {
      first_packet_timestamp_ms_ = packet.pts;
      is_first_packet_ = false;
    }

    if (packet.stream_index == video_index_) {
      packet.pts = packet.pts - first_packet_timestamp_ms_;
      auto p = std::make_shared<MediaPacket>(&packet);
      p->PacketType(MediaPacket::Type::kVideo);
      std::vector<std::string> packet_side_data;
      packet_side_data.push_back(sps_);
      packet_side_data.push_back(pps_);
      p->SetSideData(packet_side_data);
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
            pkt->pts +=
                first_audio_packet_timestamp_ms_ - first_packet_timestamp_ms_;
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