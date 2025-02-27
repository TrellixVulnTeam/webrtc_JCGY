/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_RTCP_IMPL_H_
#define WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_RTCP_IMPL_H_

#include <list>
#include <vector>

#include "webrtc/base/scoped_ptr.h"
#include "webrtc/modules/rtp_rtcp/interface/rtp_rtcp.h"
#include "webrtc/modules/rtp_rtcp/source/packet_loss_stats.h"
#include "webrtc/modules/rtp_rtcp/source/rtcp_receiver.h"
#include "webrtc/modules/rtp_rtcp/source/rtcp_sender.h"
#include "webrtc/modules/rtp_rtcp/source/rtp_sender.h"
#include "webrtc/test/testsupport/gtest_prod_util.h"

namespace webrtc {

class ModuleRtpRtcpImpl : public RtpRtcp {
 public:
  explicit ModuleRtpRtcpImpl(const RtpRtcp::Configuration& configuration);

  // Returns the number of milliseconds until the module want a worker thread to
  // call Process.
  int64_t TimeUntilNextProcess() override;

  // Process any pending tasks such as timeouts.
  int32_t Process() override;

  // Receiver part.

  // Called when we receive an RTCP packet.
  int32_t IncomingRtcpPacket(const uint8_t* incoming_packet,
                             size_t incoming_packet_length) override;

  void SetRemoteSSRC(uint32_t ssrc) override;

  // Sender part.

  int32_t RegisterSendPayload(const CodecInst& voice_codec) override;

  int32_t RegisterSendPayload(const VideoCodec& video_codec) override;

  int32_t DeRegisterSendPayload(int8_t payload_type) override;

  int8_t SendPayloadType() const;

  // Register RTP header extension.
  int32_t RegisterSendRtpHeaderExtension(RTPExtensionType type,
                                         uint8_t id) override;

  int32_t DeregisterSendRtpHeaderExtension(RTPExtensionType type) override;

  // Get start timestamp.
  uint32_t StartTimestamp() const override;

  // Configure start timestamp, default is a random number.
  void SetStartTimestamp(uint32_t timestamp) override;

  uint16_t SequenceNumber() const override;

  // Set SequenceNumber, default is a random number.
  void SetSequenceNumber(uint16_t seq) override;

  bool SetRtpStateForSsrc(uint32_t ssrc, const RtpState& rtp_state) override;
  bool GetRtpStateForSsrc(uint32_t ssrc, RtpState* rtp_state) override;

  uint32_t SSRC() const override;

  // Configure SSRC, default is a random number.
  void SetSSRC(uint32_t ssrc) override;

  void SetCsrcs(const std::vector<uint32_t>& csrcs) override;

  RTCPSender::FeedbackState GetFeedbackState();

  int CurrentSendFrequencyHz() const;

  void SetRtxSendStatus(int mode) override;
  int RtxSendStatus() const override;

  void SetRtxSsrc(uint32_t ssrc) override;

  void SetRtxSendPayloadType(int payload_type,
                             int associated_payload_type) override;
  std::pair<int, int> RtxSendPayloadType() const override;

  // Sends kRtcpByeCode when going from true to false.
  int32_t SetSendingStatus(bool sending) override;

  bool Sending() const override;

  // Drops or relays media packets.
  void SetSendingMediaStatus(bool sending) override;

  bool SendingMedia() const override;

  // Used by the codec module to deliver a video or audio frame for
  // packetization.
  int32_t SendOutgoingData(FrameType frame_type,
                           int8_t payload_type,
                           uint32_t time_stamp,
                           int64_t capture_time_ms,
                           const uint8_t* payload_data,
                           size_t payload_size,
                           const RTPFragmentationHeader* fragmentation = NULL,
                           const RTPVideoHeader* rtp_video_hdr = NULL) override;

  bool TimeToSendPacket(uint32_t ssrc,
                        uint16_t sequence_number,
                        int64_t capture_time_ms,
                        bool retransmission) override;

  // Returns the number of padding bytes actually sent, which can be more or
  // less than |bytes|.
  size_t TimeToSendPadding(size_t bytes) override;

  // RTCP part.

  // Get RTCP status.
  RTCPMethod RTCP() const override;

  // Configure RTCP status i.e on/off.
  void SetRTCPStatus(RTCPMethod method) override;

  // Set RTCP CName.
  int32_t SetCNAME(const char* c_name) override;

  // Get remote CName.
  int32_t RemoteCNAME(uint32_t remote_ssrc,
                      char c_name[RTCP_CNAME_SIZE]) const override;

  // Get remote NTP.
  int32_t RemoteNTP(uint32_t* received_ntp_secs,
                    uint32_t* received_ntp_frac,
                    uint32_t* rtcp_arrival_time_secs,
                    uint32_t* rtcp_arrival_time_frac,
                    uint32_t* rtcp_timestamp) const override;

  int32_t AddMixedCNAME(uint32_t ssrc, const char* c_name) override;

  int32_t RemoveMixedCNAME(uint32_t ssrc) override;

  // Get RoundTripTime.
  int32_t RTT(uint32_t remote_ssrc,
              int64_t* rtt,
              int64_t* avg_rtt,
              int64_t* min_rtt,
              int64_t* max_rtt) const override;

  // Force a send of an RTCP packet.
  // Normal SR and RR are triggered via the process function.
  int32_t SendRTCP(RTCPPacketType rtcpPacketType) override;

  int32_t SendCompoundRTCP(
      const std::set<RTCPPacketType>& rtcpPacketTypes) override;

  // Statistics of the amount of data sent and received.
  int32_t DataCountersRTP(size_t* bytes_sent,
                          uint32_t* packets_sent) const override;

  void GetSendStreamDataCounters(
      StreamDataCounters* rtp_counters,
      StreamDataCounters* rtx_counters) const override;

  void GetRtpPacketLossStats(
      bool outgoing,
      uint32_t ssrc,
      struct RtpPacketLossStats* loss_stats) const override;

  // Get received RTCP report, sender info.
  int32_t RemoteRTCPStat(RTCPSenderInfo* sender_info) override;

  // Get received RTCP report, report block.
  int32_t RemoteRTCPStat(
      std::vector<RTCPReportBlock>* receive_blocks) const override;

  // (REMB) Receiver Estimated Max Bitrate.
  bool REMB() const override;

  void SetREMBStatus(bool enable) override;

  void SetREMBData(uint32_t bitrate,
                   const std::vector<uint32_t>& ssrcs) override;

  // (TMMBR) Temporary Max Media Bit Rate.
  bool TMMBR() const override;

  void SetTMMBRStatus(bool enable) override;

  int32_t SetTMMBN(const TMMBRSet* bounding_set);

  uint16_t MaxPayloadLength() const override;

  uint16_t MaxDataPayloadLength() const override;

  int32_t SetMaxTransferUnit(uint16_t size) override;

  int32_t SetTransportOverhead(bool tcp,
                               bool ipv6,
                               uint8_t authentication_overhead = 0) override;

  // (NACK) Negative acknowledgment part.

  int SelectiveRetransmissions() const override;

  int SetSelectiveRetransmissions(uint8_t settings) override;

  // Send a Negative acknowledgment packet.
  int32_t SendNACK(const uint16_t* nack_list, uint16_t size) override;

  // Store the sent packets, needed to answer to a negative acknowledgment
  // requests.
  void SetStorePacketsStatus(bool enable, uint16_t number_to_store) override;

  bool StorePackets() const override;

  // Called on receipt of RTCP report block from remote side.
  void RegisterRtcpStatisticsCallback(
      RtcpStatisticsCallback* callback) override;
  RtcpStatisticsCallback* GetRtcpStatisticsCallback() override;

  bool SendFeedbackPacket(const rtcp::TransportFeedback& packet) override;
  // (APP) Application specific data.
  int32_t SetRTCPApplicationSpecificData(uint8_t sub_type,
                                         uint32_t name,
                                         const uint8_t* data,
                                         uint16_t length) override;

  // (XR) VOIP metric.
  int32_t SetRTCPVoIPMetrics(const RTCPVoIPMetric* VoIPMetric) override;

  // (XR) Receiver reference time report.
  void SetRtcpXrRrtrStatus(bool enable) override;

  bool RtcpXrRrtrStatus() const override;

  // Audio part.

  // Set audio packet size, used to determine when it's time to send a DTMF
  // packet in silence (CNG).
  int32_t SetAudioPacketSize(uint16_t packet_size_samples) override;

  // Send a TelephoneEvent tone using RFC 2833 (4733).
  int32_t SendTelephoneEventOutband(uint8_t key,
                                    uint16_t time_ms,
                                    uint8_t level) override;

  // Set payload type for Redundant Audio Data RFC 2198.
  int32_t SetSendREDPayloadType(int8_t payload_type) override;

  // Get payload type for Redundant Audio Data RFC 2198.
  int32_t SendREDPayloadType(int8_t& payload_type) const override;

  // Store the audio level in d_bov for header-extension-for-audio-level-
  // indication.
  int32_t SetAudioLevel(uint8_t level_d_bov) override;

  // Video part.

  int32_t SendRTCPSliceLossIndication(uint8_t picture_id) override;

  // Set method for requesting a new key frame.
  int32_t SetKeyFrameRequestMethod(KeyFrameRequestMethod method) override;

  // Send a request for a keyframe.
  int32_t RequestKeyFrame() override;

  void SetTargetSendBitrate(uint32_t bitrate_bps) override;

  void SetGenericFECStatus(bool enable,
                           uint8_t payload_type_red,
                           uint8_t payload_type_fec) override;

  void GenericFECStatus(bool& enable,
                        uint8_t& payload_type_red,
                        uint8_t& payload_type_fec) override;

  int32_t SetFecParameters(const FecProtectionParams* delta_params,
                           const FecProtectionParams* key_params) override;

  bool LastReceivedNTP(uint32_t* NTPsecs,
                       uint32_t* NTPfrac,
                       uint32_t* remote_sr) const;

  bool LastReceivedXrReferenceTimeInfo(RtcpReceiveTimeInfo* info) const;

  virtual int32_t BoundingSet(bool& tmmbr_owner, TMMBRSet*& bounding_set_rec);

  void BitrateSent(uint32_t* total_rate,
                   uint32_t* video_rate,
                   uint32_t* fec_rate,
                   uint32_t* nackRate) const override;

  int64_t SendTimeOfSendReport(uint32_t send_report);

  bool SendTimeOfXrRrReport(uint32_t mid_ntp, int64_t* time_ms) const;

  // Good state of RTP receiver inform sender.
  int32_t SendRTCPReferencePictureSelection(uint64_t picture_id) override;

  void RegisterSendChannelRtpStatisticsCallback(
      StreamDataCountersCallback* callback) override;
  StreamDataCountersCallback* GetSendChannelRtpStatisticsCallback()
      const override;

  void OnReceivedTMMBR();

  // Bad state of RTP receiver request a keyframe.
  void OnRequestIntraFrame();

  // Received a request for a new SLI.
  void OnReceivedSliceLossIndication(uint8_t picture_id);

  // Received a new reference frame.
  void OnReceivedReferencePictureSelectionIndication(uint64_t picture_id);

  void OnReceivedNACK(const std::list<uint16_t>& nack_sequence_numbers);

  void OnRequestSendReport();

 protected:
  bool UpdateRTCPReceiveInformationTimers();

  uint32_t BitrateReceivedNow() const;

  // Get remote SequenceNumber.
  uint16_t RemoteSequenceNumber() const;

  RTPSender rtp_sender_;

  RTCPSender rtcp_sender_;
  RTCPReceiver rtcp_receiver_;

  Clock* clock_;

 private:
  FRIEND_TEST_ALL_PREFIXES(RtpRtcpImplTest, Rtt);
  FRIEND_TEST_ALL_PREFIXES(RtpRtcpImplTest, RttForReceiverOnly);
  int64_t RtcpReportInterval();
  void SetRtcpReceiverSsrcs(uint32_t main_ssrc);

  void set_rtt_ms(int64_t rtt_ms);
  int64_t rtt_ms() const;

  bool TimeToSendFullNackList(int64_t now) const;

  int32_t id_;
  const bool audio_;
  bool collision_detected_;
  int64_t last_process_time_;
  int64_t last_bitrate_process_time_;
  int64_t last_rtt_process_time_;
  uint16_t packet_overhead_;

  size_t padding_index_;

  // Send side
  NACKMethod nack_method_;
  int64_t nack_last_time_sent_full_;
  uint32_t nack_last_time_sent_full_prev_;
  uint16_t nack_last_seq_number_sent_;

  VideoCodec send_video_codec_;
  KeyFrameRequestMethod key_frame_req_method_;

  RemoteBitrateEstimator* remote_bitrate_;

  RtcpRttStats* rtt_stats_;

  PacketLossStats send_loss_stats_;
  PacketLossStats receive_loss_stats_;

  // The processed RTT from RtcpRttStats.
  rtc::scoped_ptr<CriticalSectionWrapper> critical_section_rtt_;
  int64_t rtt_ms_;
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_RTP_RTCP_SOURCE_RTP_RTCP_IMPL_H_
