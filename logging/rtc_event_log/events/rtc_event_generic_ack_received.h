/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_GENERIC_ACK_RECEIVED_H_
#define LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_GENERIC_ACK_RECEIVED_H_

#include "logging/rtc_event_log/events/rtc_event.h"

#include <memory>
#include <vector>

namespace webrtc {

struct AckedPacket {
  // The packet number that was acked.
  int64_t packet_number;

  // The time where the packet was received. Not every ACK will
  // include the receive timestamp.
  absl::optional<int64_t> receive_timestamp_ms;
};

class RtcEventGenericAckReceived final : public RtcEvent {
 public:
  // For a collection of acked packets, it creates a vector of logs to log with
  // the same timestamp.
  static std::vector<std::unique_ptr<RtcEventGenericAckReceived>> CreateLogs(
      int64_t packet_number,
      const std::vector<AckedPacket> acked_packets);

  RtcEventGenericAckReceived(const RtcEventGenericAckReceived& packet);
  ~RtcEventGenericAckReceived() override;

  Type GetType() const override;

  bool IsConfigEvent() const override;

  // An identifier of the packet which contained an ack.
  int64_t packet_number() const { return packet_number_; }

  // An identifier of the acked packet.
  int64_t acked_packet_number() const { return acked_packet_number_; }

  // Collection of the received acks with their timestamps.
  const absl::optional<int64_t> receive_timestamp_ms() const {
    return receive_timestamp_ms_;
  }

 private:
  // When the ack is received, |packet_number| identifies the packet which
  // contained an ack for |acked_packet_number|, and contains the
  // |receive_timestamp_ms| on which the |acked_packet_number| was received on
  // the remote side. The |receive_timestamp_ms| may be null.
  RtcEventGenericAckReceived(int64_t timestamp_us,
                             int64_t packet_number,
                             int64_t acked_packet_number,
                             absl::optional<int64_t> receive_timestamp_ms);

  const int64_t packet_number_;
  const int64_t acked_packet_number_;
  const absl::optional<int64_t> receive_timestamp_ms_;
};

}  // namespace webrtc

#endif  // LOGGING_RTC_EVENT_LOG_EVENTS_RTC_EVENT_GENERIC_ACK_RECEIVED_H_
