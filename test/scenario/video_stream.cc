/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/scenario/video_stream.h"

#include <algorithm>
#include <utility>

#include "absl/memory/memory.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "media/base/media_constants.h"
#include "media/engine/internal_decoder_factory.h"
#include "media/engine/internal_encoder_factory.h"
#include "media/engine/webrtc_video_engine.h"
#include "test/call_test.h"
#include "test/fake_encoder.h"
#include "test/scenario/hardware_codecs.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {
namespace test {
namespace {
constexpr int kDefaultMaxQp = cricket::WebRtcVideoChannel::kDefaultQpMax;
const int kVideoRotationRtpExtensionId = 4;
uint8_t CodecTypeToPayloadType(VideoCodecType codec_type) {
  switch (codec_type) {
    case VideoCodecType::kVideoCodecGeneric:
      return CallTest::kFakeVideoSendPayloadType;
    case VideoCodecType::kVideoCodecVP8:
      return CallTest::kPayloadTypeVP8;
    case VideoCodecType::kVideoCodecVP9:
      return CallTest::kPayloadTypeVP9;
    case VideoCodecType::kVideoCodecH264:
      return CallTest::kPayloadTypeH264;
    default:
      RTC_NOTREACHED();
  }
  return {};
}
std::string CodecTypeToCodecName(VideoCodecType codec_type) {
  switch (codec_type) {
    case VideoCodecType::kVideoCodecGeneric:
      return "";
    case VideoCodecType::kVideoCodecVP8:
      return cricket::kVp8CodecName;
    case VideoCodecType::kVideoCodecVP9:
      return cricket::kVp9CodecName;
    case VideoCodecType::kVideoCodecH264:
      return cricket::kH264CodecName;
    default:
      RTC_NOTREACHED();
  }
  return {};
}
std::vector<RtpExtension> GetVideoRtpExtensions(
    const VideoStreamConfig config) {
  return {RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                       kTransportSequenceNumberExtensionId),
          RtpExtension(RtpExtension::kVideoContentTypeUri,
                       kVideoContentTypeExtensionId),
          RtpExtension(RtpExtension::kVideoRotationUri,
                       kVideoRotationRtpExtensionId)};
}

VideoSendStream::Config CreateVideoSendStreamConfig(VideoStreamConfig config,
                                                    std::vector<uint32_t> ssrcs,
                                                    Transport* send_transport) {
  VideoSendStream::Config send_config(send_transport);
  send_config.rtp.payload_name = CodecTypeToPayloadString(config.encoder.codec);
  send_config.rtp.payload_type = CodecTypeToPayloadType(config.encoder.codec);

  send_config.rtp.ssrcs = ssrcs;
  send_config.rtp.extensions = GetVideoRtpExtensions(config);

  if (config.stream.use_flexfec) {
    send_config.rtp.flexfec.payload_type = CallTest::kFlexfecPayloadType;
    send_config.rtp.flexfec.ssrc = CallTest::kFlexfecSendSsrc;
    send_config.rtp.flexfec.protected_media_ssrcs = ssrcs;
  }
  if (config.stream.use_ulpfec) {
    send_config.rtp.ulpfec.red_payload_type = CallTest::kRedPayloadType;
    send_config.rtp.ulpfec.ulpfec_payload_type = CallTest::kUlpfecPayloadType;
    send_config.rtp.ulpfec.red_rtx_payload_type = CallTest::kRtxRedPayloadType;
  }
  return send_config;
}
rtc::scoped_refptr<VideoEncoderConfig::EncoderSpecificSettings>
CreateEncoderSpecificSettings(VideoStreamConfig config) {
  using Codec = VideoStreamConfig::Encoder::Codec;
  switch (config.encoder.codec) {
    case Codec::kVideoCodecH264: {
      VideoCodecH264 h264_settings = VideoEncoder::GetDefaultH264Settings();
      h264_settings.frameDroppingOn = true;
      h264_settings.keyFrameInterval =
          config.encoder.key_frame_interval.value_or(0);
      return new rtc::RefCountedObject<
          VideoEncoderConfig::H264EncoderSpecificSettings>(h264_settings);
    }
    case Codec::kVideoCodecVP8: {
      VideoCodecVP8 vp8_settings = VideoEncoder::GetDefaultVp8Settings();
      vp8_settings.frameDroppingOn = true;
      vp8_settings.keyFrameInterval =
          config.encoder.key_frame_interval.value_or(0);
      vp8_settings.automaticResizeOn = true;
      vp8_settings.denoisingOn = config.encoder.denoising;
      return new rtc::RefCountedObject<
          VideoEncoderConfig::Vp8EncoderSpecificSettings>(vp8_settings);
    }
    case Codec::kVideoCodecVP9: {
      VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
      vp9_settings.frameDroppingOn = true;
      vp9_settings.keyFrameInterval =
          config.encoder.key_frame_interval.value_or(0);
      vp9_settings.automaticResizeOn = true;
      vp9_settings.denoisingOn = config.encoder.denoising;
      vp9_settings.interLayerPred = InterLayerPredMode::kOnKeyPic;
      return new rtc::RefCountedObject<
          VideoEncoderConfig::Vp9EncoderSpecificSettings>(vp9_settings);
    }
    default:
      return nullptr;
  }
}

VideoEncoderConfig CreateVideoEncoderConfig(VideoStreamConfig config) {
  size_t num_streams = config.encoder.num_simulcast_streams;
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = config.encoder.codec;
  switch (config.source.content_type) {
    case VideoStreamConfig::Source::ContentType::kVideo:
      encoder_config.content_type =
          VideoEncoderConfig::ContentType::kRealtimeVideo;
      break;
    case VideoStreamConfig::Source::ContentType::kScreen:
      encoder_config.content_type = VideoEncoderConfig::ContentType::kScreen;
      break;
  }
  encoder_config.video_format =
      SdpVideoFormat(CodecTypeToPayloadString(config.encoder.codec), {});
  encoder_config.number_of_streams = num_streams;
  encoder_config.simulcast_layers = std::vector<VideoStream>(num_streams);
  encoder_config.min_transmit_bitrate_bps = config.stream.pad_to_rate.bps();

  std::string cricket_codec = CodecTypeToCodecName(config.encoder.codec);
  if (!cricket_codec.empty()) {
    encoder_config.video_stream_factory =
        new rtc::RefCountedObject<cricket::EncoderStreamFactory>(
            cricket_codec, kDefaultMaxQp, false, false);
  } else {
    encoder_config.video_stream_factory =
        new rtc::RefCountedObject<DefaultVideoStreamFactory>();
  }
  if (config.encoder.max_data_rate) {
    encoder_config.max_bitrate_bps = config.encoder.max_data_rate->bps();
  } else {
    encoder_config.max_bitrate_bps = 10000000;  // 10 mbit
  }
  encoder_config.encoder_specific_settings =
      CreateEncoderSpecificSettings(config);
  if (config.encoder.max_framerate) {
    for (auto& layer : encoder_config.simulcast_layers) {
      layer.max_framerate = *config.encoder.max_framerate;
    }
  }

  return encoder_config;
}
}  // namespace

SendVideoStream::SendVideoStream(CallClient* sender,
                                 VideoStreamConfig config,
                                 Transport* send_transport,
                                 VideoQualityAnalyzer* analyzer)
    : sender_(sender), config_(config) {
  for (size_t i = 0; i < config.encoder.num_simulcast_streams; ++i) {
    ssrcs_.push_back(sender->GetNextVideoSsrc());
    rtx_ssrcs_.push_back(sender->GetNextRtxSsrc());
  }

  using Capture = VideoStreamConfig::Source::Capture;
  switch (config.source.capture) {
    case Capture::kGenerator:
      frame_generator_ = test::FrameGeneratorCapturer::Create(
          config.source.width, config.source.height,
          config.source.generator.pixel_format, absl::nullopt,
          config.source.framerate, sender_->clock_);
      video_capturer_.reset(frame_generator_);
      break;
    case Capture::kVideoFile:
      frame_generator_ = test::FrameGeneratorCapturer::CreateFromYuvFile(
          test::ResourcePath(config.source.video_file.name, "yuv"),
          config.source.width, config.source.height, config.source.framerate,
          sender_->clock_);
      RTC_CHECK(frame_generator_)
          << "Could not create capturer for " << config.source.video_file.name
          << ".yuv. Is this resource file present?";
      video_capturer_.reset(frame_generator_);
      break;
  }

  using Encoder = VideoStreamConfig::Encoder;
  using Codec = VideoStreamConfig::Encoder::Codec;
  switch (config.encoder.implementation) {
    case Encoder::Implementation::kFake:
      if (config.encoder.codec == Codec::kVideoCodecGeneric) {
        encoder_factory_ =
            absl::make_unique<FunctionVideoEncoderFactory>([this]() {
              rtc::CritScope cs(&crit_);
              auto encoder =
                  absl::make_unique<test::FakeEncoder>(sender_->clock_);
              fake_encoders_.push_back(encoder.get());
              if (config_.encoder.fake.max_rate.IsFinite())
                encoder->SetMaxBitrate(config_.encoder.fake.max_rate.kbps());
              return encoder;
            });
      } else {
        RTC_NOTREACHED();
      }
      break;
    case VideoStreamConfig::Encoder::Implementation::kSoftware:
      encoder_factory_.reset(new InternalEncoderFactory());
      break;
    case VideoStreamConfig::Encoder::Implementation::kHardware:
      encoder_factory_ = CreateHardwareEncoderFactory();
      break;
  }
  RTC_CHECK(encoder_factory_);

  bitrate_allocator_factory_ = CreateBuiltinVideoBitrateAllocatorFactory();
  RTC_CHECK(bitrate_allocator_factory_);

  VideoSendStream::Config send_config =
      CreateVideoSendStreamConfig(config, ssrcs_, send_transport);
  send_config.encoder_settings.encoder_factory = encoder_factory_.get();
  send_config.encoder_settings.bitrate_allocator_factory =
      bitrate_allocator_factory_.get();

  VideoEncoderConfig encoder_config = CreateVideoEncoderConfig(config);

  send_stream_ = sender_->call_->CreateVideoSendStream(
      std::move(send_config), std::move(encoder_config));
  std::vector<std::function<void(const VideoFrameQualityInfo&)> >
      frame_info_handlers;
  if (config.analyzer.frame_quality_handler)
    frame_info_handlers.push_back(config.analyzer.frame_quality_handler);

  if (analyzer->Active()) {
    frame_tap_.reset(new ForwardingCapturedFrameTap(sender_->clock_, analyzer,
                                                    video_capturer_.get()));
    send_stream_->SetSource(frame_tap_.get(),
                            config.encoder.degradation_preference);
  } else {
    send_stream_->SetSource(video_capturer_.get(),
                            config.encoder.degradation_preference);
  }
}

SendVideoStream::~SendVideoStream() {
  sender_->call_->DestroyVideoSendStream(send_stream_);
}

void SendVideoStream::Start() {
  send_stream_->Start();
  sender_->call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
}

void SendVideoStream::Stop() {
  send_stream_->Stop();
}

void SendVideoStream::UpdateConfig(
    std::function<void(VideoStreamConfig*)> modifier) {
  rtc::CritScope cs(&crit_);
  VideoStreamConfig prior_config = config_;
  modifier(&config_);
  if (prior_config.encoder.fake.max_rate != config_.encoder.fake.max_rate) {
    for (auto* encoder : fake_encoders_) {
      encoder->SetMaxBitrate(config_.encoder.fake.max_rate.kbps());
    }
  }
  // TODO(srte): Add more conditions that should cause reconfiguration.
  if (prior_config.encoder.max_framerate != config_.encoder.max_framerate) {
    VideoEncoderConfig encoder_config = CreateVideoEncoderConfig(config_);
    send_stream_->ReconfigureVideoEncoder(std::move(encoder_config));
  }
  if (prior_config.source.framerate != config_.source.framerate) {
    SetCaptureFramerate(config_.source.framerate);
  }
}

void SendVideoStream::SetCaptureFramerate(int framerate) {
  RTC_CHECK(frame_generator_)
      << "Framerate change only implemented for generators";
  frame_generator_->ChangeFramerate(framerate);
}

VideoSendStream::Stats SendVideoStream::GetStats() const {
  return send_stream_->GetStats();
}

ColumnPrinter SendVideoStream::StatsPrinter() {
  return ColumnPrinter::Lambda(
      "video_target_rate video_sent_rate width height",
      [this](rtc::SimpleStringBuilder& sb) {
        VideoSendStream::Stats video_stats = send_stream_->GetStats();
        int width = 0;
        int height = 0;
        for (const auto& stream_stat : video_stats.substreams) {
          width = std::max(width, stream_stat.second.width);
          height = std::max(height, stream_stat.second.height);
        }
        sb.AppendFormat("%.0lf %.0lf %i %i",
                        video_stats.target_media_bitrate_bps / 8.0,
                        video_stats.media_bitrate_bps / 8.0, width, height);
      },
      64);
}

ReceiveVideoStream::ReceiveVideoStream(CallClient* receiver,
                                       VideoStreamConfig config,
                                       SendVideoStream* send_stream,
                                       size_t chosen_stream,
                                       Transport* feedback_transport,
                                       VideoQualityAnalyzer* analyzer)
    : receiver_(receiver), config_(config) {
  if (analyzer->Active()) {
    renderer_ = absl::make_unique<DecodedFrameTap>(analyzer);
  } else {
    renderer_ = absl::make_unique<FakeVideoRenderer>();
  }
  VideoReceiveStream::Config recv_config(feedback_transport);
  recv_config.rtp.remb = !config.stream.packet_feedback;
  recv_config.rtp.transport_cc = config.stream.packet_feedback;
  recv_config.rtp.local_ssrc = CallTest::kReceiverLocalVideoSsrc;
  recv_config.rtp.extensions = GetVideoRtpExtensions(config);
  receiver_->AddExtensions(recv_config.rtp.extensions);
  RTC_DCHECK(!config.stream.use_rtx ||
             config.stream.nack_history_time > TimeDelta::Zero());
  recv_config.rtp.nack.rtp_history_ms = config.stream.nack_history_time.ms();
  recv_config.rtp.protected_by_flexfec = config.stream.use_flexfec;
  recv_config.renderer = renderer_.get();
  if (config.stream.use_rtx) {
    recv_config.rtp.rtx_ssrc = send_stream->rtx_ssrcs_[chosen_stream];
    receiver->ssrc_media_types_[recv_config.rtp.rtx_ssrc] = MediaType::VIDEO;
    recv_config.rtp
        .rtx_associated_payload_types[CallTest::kSendRtxPayloadType] =
        CodecTypeToPayloadType(config.encoder.codec);
  }
  recv_config.rtp.remote_ssrc = send_stream->ssrcs_[chosen_stream];
  receiver->ssrc_media_types_[recv_config.rtp.remote_ssrc] = MediaType::VIDEO;

  VideoReceiveStream::Decoder decoder =
      CreateMatchingDecoder(CodecTypeToPayloadType(config.encoder.codec),
                            CodecTypeToPayloadString(config.encoder.codec));
  if (config.encoder.codec ==
      VideoStreamConfig::Encoder::Codec::kVideoCodecGeneric) {
    decoder_factory_ = absl::make_unique<FunctionVideoDecoderFactory>(
        []() { return absl::make_unique<FakeDecoder>(); });
  } else {
    decoder_factory_ = absl::make_unique<InternalDecoderFactory>();
  }
  decoder.decoder_factory = decoder_factory_.get();
  recv_config.decoders.push_back(decoder);

  if (config.stream.use_flexfec) {
    RTC_CHECK_EQ(config.encoder.num_simulcast_streams, 1);
    FlexfecReceiveStream::Config flexfec_config(feedback_transport);
    flexfec_config.payload_type = CallTest::kFlexfecPayloadType;
    flexfec_config.remote_ssrc = CallTest::kFlexfecSendSsrc;
    receiver->ssrc_media_types_[flexfec_config.remote_ssrc] = MediaType::VIDEO;
    flexfec_config.protected_media_ssrcs = send_stream->rtx_ssrcs_;
    flexfec_config.local_ssrc = recv_config.rtp.local_ssrc;
    flecfec_stream_ =
        receiver_->call_->CreateFlexfecReceiveStream(flexfec_config);
  }
  if (config.stream.use_ulpfec) {
    recv_config.rtp.red_payload_type = CallTest::kRedPayloadType;
    recv_config.rtp.ulpfec_payload_type = CallTest::kUlpfecPayloadType;
    recv_config.rtp.rtx_associated_payload_types[CallTest::kRtxRedPayloadType] =
        CallTest::kRedPayloadType;
  }
  receive_stream_ =
      receiver_->call_->CreateVideoReceiveStream(std::move(recv_config));
}

ReceiveVideoStream::~ReceiveVideoStream() {
  receiver_->call_->DestroyVideoReceiveStream(receive_stream_);
  if (flecfec_stream_)
    receiver_->call_->DestroyFlexfecReceiveStream(flecfec_stream_);
}

void ReceiveVideoStream::Start() {
  receive_stream_->Start();
  receiver_->call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
}

void ReceiveVideoStream::Stop() {
  receive_stream_->Stop();
}

VideoStreamPair::~VideoStreamPair() = default;

VideoStreamPair::VideoStreamPair(
    CallClient* sender,
    CallClient* receiver,
    VideoStreamConfig config,
    std::unique_ptr<RtcEventLogOutput> quality_writer)
    : config_(config),
      analyzer_(std::move(quality_writer),
                config.analyzer.frame_quality_handler),
      send_stream_(sender, config, &sender->transport_, &analyzer_),
      receive_stream_(receiver,
                      config,
                      &send_stream_,
                      /*chosen_stream=*/0,
                      &receiver->transport_,
                      &analyzer_) {}

}  // namespace test
}  // namespace webrtc
