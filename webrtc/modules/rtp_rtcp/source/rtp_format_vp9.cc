/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/rtp_rtcp/source/rtp_format_vp9.h"

#include <assert.h>
#include <string.h>

#include <cmath>

#include "webrtc/base/bitbuffer.h"
#include "webrtc/base/checks.h"
#include "webrtc/system_wrappers/interface/logging.h"

#define RETURN_FALSE_ON_ERROR(x) \
  if (!(x)) {                    \
    return false;                \
  }

namespace webrtc {
namespace {
// Length of VP9 payload descriptors' fixed part.
const size_t kFixedPayloadDescriptorBytes = 1;

// Packet fragmentation mode. If true, packets are split into (almost) equal
// sizes. Otherwise, as many bytes as possible are fit into one packet.
const bool kBalancedMode = true;

const uint32_t kReservedBitValue0 = 0;

uint8_t TemporalIdxField(const RTPVideoHeaderVP9& hdr, uint8_t def) {
  return (hdr.temporal_idx == kNoTemporalIdx) ? def : hdr.temporal_idx;
}

uint8_t SpatialIdxField(const RTPVideoHeaderVP9& hdr, uint8_t def) {
  return (hdr.spatial_idx == kNoSpatialIdx) ? def : hdr.spatial_idx;
}

int16_t Tl0PicIdxField(const RTPVideoHeaderVP9& hdr, uint8_t def) {
  return (hdr.tl0_pic_idx == kNoTl0PicIdx) ? def : hdr.tl0_pic_idx;
}

uint8_t GofIdxField(const RTPVideoHeaderVP9& hdr, uint8_t def) {
  return (hdr.gof_idx == kNoGofIdx) ? def : hdr.gof_idx;
}

// Picture ID:
//
//      +-+-+-+-+-+-+-+-+
// I:   |M| PICTURE ID  |   M:0 => picture id is 7 bits.
//      +-+-+-+-+-+-+-+-+   M:1 => picture id is 15 bits.
// M:   | EXTENDED PID  |
//      +-+-+-+-+-+-+-+-+
//
size_t PictureIdLength(const RTPVideoHeaderVP9& hdr) {
  if (hdr.picture_id == kNoPictureId)
    return 0;
  return (hdr.max_picture_id == kMaxOneBytePictureId) ? 1 : 2;
}

bool PictureIdPresent(const RTPVideoHeaderVP9& hdr) {
  return PictureIdLength(hdr) > 0;
}

// Layer indices:
//
// Flexible mode (F=1):     Non-flexible mode (F=0):
//
//      +-+-+-+-+-+-+-+-+   +-+-+-+-+-+-+-+-+
// L:   |  T  |U|  S  |D|   |GOF_IDX|  S  |D|
//      +-+-+-+-+-+-+-+-+   +-+-+-+-+-+-+-+-+
//                          |   TL0PICIDX   |
//                          +-+-+-+-+-+-+-+-+
//
size_t LayerInfoLength(const RTPVideoHeaderVP9& hdr) {
  if (hdr.flexible_mode) {
    return (hdr.temporal_idx == kNoTemporalIdx &&
            hdr.spatial_idx == kNoSpatialIdx) ? 0 : 1;
  } else {
    return (hdr.gof_idx == kNoGofIdx &&
            hdr.spatial_idx == kNoSpatialIdx) ? 0 : 2;
  }
}

bool LayerInfoPresent(const RTPVideoHeaderVP9& hdr) {
  return LayerInfoLength(hdr) > 0;
}

// Reference indices:
//
//      +-+-+-+-+-+-+-+-+  -|           P=1,F=1: At least one reference index
// P,F: | P_DIFF    |X|N|   .                    has to be specified.
//      +-+-+-+-+-+-+-+-+   . up to 3 times
// X:   |EXTENDED P_DIFF|   .               X=1: Extended P_DIFF is used (14
//      +-+-+-+-+-+-+-+-+  -|                    bits). Else 6 bits are used.
//                                          N=1: An additional P_DIFF follows
//                                               current P_DIFF.
size_t RefIndicesLength(const RTPVideoHeaderVP9& hdr) {
  if (!hdr.inter_pic_predicted || !hdr.flexible_mode)
    return 0;

  DCHECK_GT(hdr.num_ref_pics, 0U);
  DCHECK_LE(hdr.num_ref_pics, kMaxVp9RefPics);
  size_t length = 0;
  for (size_t i = 0; i < hdr.num_ref_pics; ++i) {
    length += hdr.pid_diff[i] > 0x3F ? 2 : 1;   // P_DIFF > 6 bits => extended
  }
  return length;
}

// Scalability structure (SS).
//
//      +-+-+-+-+-+-+-+-+
// V:   | N_S |Y|  N_G  |
//      +-+-+-+-+-+-+-+-+              -|
// Y:   |     WIDTH     | (OPTIONAL)    .
//      +               +               .
//      |               | (OPTIONAL)    .
//      +-+-+-+-+-+-+-+-+               . N_S + 1 times
//      |     HEIGHT    | (OPTIONAL)    .
//      +               +               .
//      |               | (OPTIONAL)    .
//      +-+-+-+-+-+-+-+-+              -|           -|
// N_G: |  T  |U| R |-|-| (OPTIONAL)                 .
//      +-+-+-+-+-+-+-+-+              -|            . N_G + 1 times
//      |    P_DIFF     | (OPTIONAL)    . R times    .
//      +-+-+-+-+-+-+-+-+              -|           -|
//
size_t SsDataLength(const RTPVideoHeaderVP9& hdr) {
  if (!hdr.ss_data_available)
    return 0;

  DCHECK_GT(hdr.num_spatial_layers, 0U);
  DCHECK_LE(hdr.num_spatial_layers, kMaxVp9NumberOfSpatialLayers);
  DCHECK_GT(hdr.gof.num_frames_in_gof, 0U);
  DCHECK_LE(hdr.gof.num_frames_in_gof, kMaxVp9FramesInGof);
  size_t length = 1;                           // V
  if (hdr.spatial_layer_resolution_present) {
    length += 4 * hdr.num_spatial_layers;      // Y
  }
  // N_G
  length += hdr.gof.num_frames_in_gof;  // T, U, R
  for (size_t i = 0; i < hdr.gof.num_frames_in_gof; ++i) {
    DCHECK_LE(hdr.gof.num_ref_pics[i], kMaxVp9RefPics);
    length += hdr.gof.num_ref_pics[i];  // R times
  }
  return length;
}

size_t PayloadDescriptorLengthMinusSsData(const RTPVideoHeaderVP9& hdr) {
  return kFixedPayloadDescriptorBytes + PictureIdLength(hdr) +
         LayerInfoLength(hdr) + RefIndicesLength(hdr);
}

size_t PayloadDescriptorLength(const RTPVideoHeaderVP9& hdr) {
  return PayloadDescriptorLengthMinusSsData(hdr) + SsDataLength(hdr);
}

void QueuePacket(size_t start_pos,
                 size_t size,
                 bool layer_begin,
                 bool layer_end,
                 RtpPacketizerVp9::PacketInfoQueue* packets) {
  RtpPacketizerVp9::PacketInfo packet_info;
  packet_info.payload_start_pos = start_pos;
  packet_info.size = size;
  packet_info.layer_begin = layer_begin;
  packet_info.layer_end = layer_end;
  packets->push(packet_info);
}

// Picture ID:
//
//      +-+-+-+-+-+-+-+-+
// I:   |M| PICTURE ID  |   M:0 => picture id is 7 bits.
//      +-+-+-+-+-+-+-+-+   M:1 => picture id is 15 bits.
// M:   | EXTENDED PID  |
//      +-+-+-+-+-+-+-+-+
//
bool WritePictureId(const RTPVideoHeaderVP9& vp9,
                    rtc::BitBufferWriter* writer) {
  bool m_bit = (PictureIdLength(vp9) == 2);
  RETURN_FALSE_ON_ERROR(writer->WriteBits(m_bit ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.picture_id, m_bit ? 15 : 7));
  return true;
}

// Layer indices:
//
// Flexible mode (F=1):
//
//      +-+-+-+-+-+-+-+-+
// L:   |  T  |U|  S  |D|
//      +-+-+-+-+-+-+-+-+
//
bool WriteLayerInfoFlexibleMode(const RTPVideoHeaderVP9& vp9,
                                rtc::BitBufferWriter* writer) {
  RETURN_FALSE_ON_ERROR(writer->WriteBits(TemporalIdxField(vp9, 0), 3));
  RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.temporal_up_switch ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer->WriteBits(SpatialIdxField(vp9, 0), 3));
  RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.inter_layer_predicted ? 1: 0, 1));
  return true;
}

// Non-flexible mode (F=0):
//
//      +-+-+-+-+-+-+-+-+
// L:   |GOF_IDX|  S  |D|
//      +-+-+-+-+-+-+-+-+
//      |   TL0PICIDX   |
//      +-+-+-+-+-+-+-+-+
//
bool WriteLayerInfoNonFlexibleMode(const RTPVideoHeaderVP9& vp9,
                                   rtc::BitBufferWriter* writer) {
  RETURN_FALSE_ON_ERROR(writer->WriteBits(GofIdxField(vp9, 0), 4));
  RETURN_FALSE_ON_ERROR(writer->WriteBits(SpatialIdxField(vp9, 0), 3));
  RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.inter_layer_predicted ? 1: 0, 1));
  RETURN_FALSE_ON_ERROR(writer->WriteUInt8(Tl0PicIdxField(vp9, 0)));
  return true;
}

bool WriteLayerInfo(const RTPVideoHeaderVP9& vp9,
                    rtc::BitBufferWriter* writer) {
  if (vp9.flexible_mode) {
    return WriteLayerInfoFlexibleMode(vp9, writer);
  } else {
    return WriteLayerInfoNonFlexibleMode(vp9, writer);
  }
}

// Reference indices:
//
//      +-+-+-+-+-+-+-+-+  -|           P=1,F=1: At least one reference index
// P,F: | P_DIFF    |X|N|   .                    has to be specified.
//      +-+-+-+-+-+-+-+-+   . up to 3 times
// X:   |EXTENDED P_DIFF|   .               X=1: Extended P_DIFF is used (14
//      +-+-+-+-+-+-+-+-+  -|                    bits). Else 6 bits are used.
//                                          N=1: An additional P_DIFF follows
//                                               current P_DIFF.
bool WriteRefIndices(const RTPVideoHeaderVP9& vp9,
                     rtc::BitBufferWriter* writer) {
  if (!PictureIdPresent(vp9) ||
      vp9.num_ref_pics == 0 || vp9.num_ref_pics > kMaxVp9RefPics) {
    return false;
  }
  for (size_t i = 0; i < vp9.num_ref_pics; ++i) {
    bool x_bit = (vp9.pid_diff[i] > 0x3F);
    bool n_bit = !(i == vp9.num_ref_pics - 1);
    if (x_bit) {
      RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.pid_diff[i] >> 8, 6));
      RETURN_FALSE_ON_ERROR(writer->WriteBits(x_bit ? 1 : 0, 1));
      RETURN_FALSE_ON_ERROR(writer->WriteBits(n_bit ? 1 : 0, 1));
      RETURN_FALSE_ON_ERROR(writer->WriteUInt8(vp9.pid_diff[i]));
    } else {
      RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.pid_diff[i], 6));
      RETURN_FALSE_ON_ERROR(writer->WriteBits(x_bit ? 1 : 0, 1));
      RETURN_FALSE_ON_ERROR(writer->WriteBits(n_bit ? 1 : 0, 1));
    }
  }
  return true;
}

// Scalability structure (SS).
//
//      +-+-+-+-+-+-+-+-+
// V:   | N_S |Y|  N_G  |
//      +-+-+-+-+-+-+-+-+              -|
// Y:   |     WIDTH     | (OPTIONAL)    .
//      +               +               .
//      |               | (OPTIONAL)    .
//      +-+-+-+-+-+-+-+-+               . N_S + 1 times
//      |     HEIGHT    | (OPTIONAL)    .
//      +               +               .
//      |               | (OPTIONAL)    .
//      +-+-+-+-+-+-+-+-+              -|           -|
// N_G: |  T  |U| R |-|-| (OPTIONAL)                 .
//      +-+-+-+-+-+-+-+-+              -|            . N_G + 1 times
//      |    P_DIFF     | (OPTIONAL)    . R times    .
//      +-+-+-+-+-+-+-+-+              -|           -|
//
bool WriteSsData(const RTPVideoHeaderVP9& vp9, rtc::BitBufferWriter* writer) {
  DCHECK_GT(vp9.num_spatial_layers, 0U);
  DCHECK_LE(vp9.num_spatial_layers, kMaxVp9NumberOfSpatialLayers);
  DCHECK_GT(vp9.gof.num_frames_in_gof, 0U);
  DCHECK_LE(vp9.gof.num_frames_in_gof, kMaxVp9FramesInGof);

  RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.num_spatial_layers - 1, 3));
  RETURN_FALSE_ON_ERROR(
      writer->WriteBits(vp9.spatial_layer_resolution_present ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.gof.num_frames_in_gof - 1, 4));

  if (vp9.spatial_layer_resolution_present) {
    for (size_t i = 0; i < vp9.num_spatial_layers; ++i) {
      RETURN_FALSE_ON_ERROR(writer->WriteUInt16(vp9.width[i]));
      RETURN_FALSE_ON_ERROR(writer->WriteUInt16(vp9.height[i]));
    }
  }
  for (size_t i = 0; i < vp9.gof.num_frames_in_gof; ++i) {
    RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.gof.temporal_idx[i], 3));
    RETURN_FALSE_ON_ERROR(
        writer->WriteBits(vp9.gof.temporal_up_switch[i] ? 1 : 0, 1));
    RETURN_FALSE_ON_ERROR(writer->WriteBits(vp9.gof.num_ref_pics[i], 2));
    RETURN_FALSE_ON_ERROR(writer->WriteBits(kReservedBitValue0, 2));
    for (size_t r = 0; r < vp9.gof.num_ref_pics[i]; ++r) {
      RETURN_FALSE_ON_ERROR(writer->WriteUInt8(vp9.gof.pid_diff[i][r]));
    }
  }
  return true;
}

// Picture ID:
//
//      +-+-+-+-+-+-+-+-+
// I:   |M| PICTURE ID  |   M:0 => picture id is 7 bits.
//      +-+-+-+-+-+-+-+-+   M:1 => picture id is 15 bits.
// M:   | EXTENDED PID  |
//      +-+-+-+-+-+-+-+-+
//
bool ParsePictureId(rtc::BitBuffer* parser, RTPVideoHeaderVP9* vp9) {
  uint32_t picture_id;
  uint32_t m_bit;
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&m_bit, 1));
  if (m_bit) {
    RETURN_FALSE_ON_ERROR(parser->ReadBits(&picture_id, 15));
    vp9->max_picture_id = kMaxTwoBytePictureId;
  } else {
    RETURN_FALSE_ON_ERROR(parser->ReadBits(&picture_id, 7));
    vp9->max_picture_id = kMaxOneBytePictureId;
  }
  vp9->picture_id = picture_id;
  return true;
}

// Layer indices (flexible mode):
//
//      +-+-+-+-+-+-+-+-+
// L:   |  T  |U|  S  |D|
//      +-+-+-+-+-+-+-+-+
//
bool ParseLayerInfoFlexibleMode(rtc::BitBuffer* parser,
                                RTPVideoHeaderVP9* vp9) {
  uint32_t t, u_bit, s, d_bit;
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&t, 3));
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&u_bit, 1));
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&s, 3));
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&d_bit, 1));
  vp9->temporal_idx = t;
  vp9->temporal_up_switch = u_bit ? true : false;
  vp9->spatial_idx = s;
  vp9->inter_layer_predicted = d_bit ? true : false;
  return true;
}

// Layer indices (non-flexible mode):
//
//      +-+-+-+-+-+-+-+-+
// L:   |GOF_IDX|  S  |D|
//      +-+-+-+-+-+-+-+-+
//      |   TL0PICIDX   |
//      +-+-+-+-+-+-+-+-+
//
bool ParseLayerInfoNonFlexibleMode(rtc::BitBuffer* parser,
                                   RTPVideoHeaderVP9* vp9) {
  uint32_t gof_idx, s, d_bit;
  uint8_t tl0picidx;
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&gof_idx, 4));
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&s, 3));
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&d_bit, 1));
  RETURN_FALSE_ON_ERROR(parser->ReadUInt8(&tl0picidx));
  vp9->gof_idx = gof_idx;
  vp9->spatial_idx = s;
  vp9->inter_layer_predicted = d_bit ? true : false;
  vp9->tl0_pic_idx = tl0picidx;
  return true;
}

bool ParseLayerInfo(rtc::BitBuffer* parser, RTPVideoHeaderVP9* vp9) {
  if (vp9->flexible_mode) {
    return ParseLayerInfoFlexibleMode(parser, vp9);
  } else {
    return ParseLayerInfoNonFlexibleMode(parser, vp9);
  }
}

// Reference indices:
//
//      +-+-+-+-+-+-+-+-+  -|           P=1,F=1: At least one reference index
// P,F: | P_DIFF    |X|N|   .                    has to be specified.
//      +-+-+-+-+-+-+-+-+   . up to 3 times
// X:   |EXTENDED P_DIFF|   .               X=1: Extended P_DIFF is used (14
//      +-+-+-+-+-+-+-+-+  -|                    bits). Else 6 bits are used.
//                                          N=1: An additional P_DIFF follows
//                                               current P_DIFF.
bool ParseRefIndices(rtc::BitBuffer* parser, RTPVideoHeaderVP9* vp9) {
  if (vp9->picture_id == kNoPictureId)
    return false;

  vp9->num_ref_pics = 0;
  uint32_t n_bit;
  do {
    if (vp9->num_ref_pics == kMaxVp9RefPics)
      return false;

    uint32_t p_diff, x_bit;
    RETURN_FALSE_ON_ERROR(parser->ReadBits(&p_diff, 6));
    RETURN_FALSE_ON_ERROR(parser->ReadBits(&x_bit, 1));
    RETURN_FALSE_ON_ERROR(parser->ReadBits(&n_bit, 1));

    if (x_bit) {
      // P_DIFF is 14 bits.
      uint8_t ext_p_diff;
      RETURN_FALSE_ON_ERROR(parser->ReadUInt8(&ext_p_diff));
      p_diff = (p_diff << 8) + ext_p_diff;
    }

    vp9->pid_diff[vp9->num_ref_pics] = p_diff;
    uint32_t scaled_pid = vp9->picture_id;
    while (p_diff > scaled_pid) {
      scaled_pid += vp9->max_picture_id + 1;
    }
    vp9->ref_picture_id[vp9->num_ref_pics++] = scaled_pid - p_diff;
  } while (n_bit);

  return true;
}

// Scalability structure (SS).
//
//      +-+-+-+-+-+-+-+-+
// V:   | N_S |Y|  N_G  |
//      +-+-+-+-+-+-+-+-+              -|
// Y:   |     WIDTH     | (OPTIONAL)    .
//      +               +               .
//      |               | (OPTIONAL)    .
//      +-+-+-+-+-+-+-+-+               . N_S + 1 times
//      |     HEIGHT    | (OPTIONAL)    .
//      +               +               .
//      |               | (OPTIONAL)    .
//      +-+-+-+-+-+-+-+-+              -|           -|
// N_G: |  T  |U| R |-|-| (OPTIONAL)                 .
//      +-+-+-+-+-+-+-+-+              -|            . N_G + 1 times
//      |    P_DIFF     | (OPTIONAL)    . R times    .
//      +-+-+-+-+-+-+-+-+              -|           -|
//
bool ParseSsData(rtc::BitBuffer* parser, RTPVideoHeaderVP9* vp9) {
  uint32_t n_s, y_bit, n_g;
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&n_s, 3));
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&y_bit, 1));
  RETURN_FALSE_ON_ERROR(parser->ReadBits(&n_g, 4));
  vp9->num_spatial_layers = n_s + 1;
  vp9->spatial_layer_resolution_present = y_bit ? true : false;
  vp9->gof.num_frames_in_gof = n_g + 1;

  if (y_bit) {
    for (size_t i = 0; i < vp9->num_spatial_layers; ++i) {
      RETURN_FALSE_ON_ERROR(parser->ReadUInt16(&vp9->width[i]));
      RETURN_FALSE_ON_ERROR(parser->ReadUInt16(&vp9->height[i]));
    }
  }
  for (size_t i = 0; i < vp9->gof.num_frames_in_gof; ++i) {
    uint32_t t, u_bit, r;
    RETURN_FALSE_ON_ERROR(parser->ReadBits(&t, 3));
    RETURN_FALSE_ON_ERROR(parser->ReadBits(&u_bit, 1));
    RETURN_FALSE_ON_ERROR(parser->ReadBits(&r, 2));
    RETURN_FALSE_ON_ERROR(parser->ConsumeBits(2));
    vp9->gof.temporal_idx[i] = t;
    vp9->gof.temporal_up_switch[i] = u_bit ? true : false;
    vp9->gof.num_ref_pics[i] = r;

    for (size_t p = 0; p < vp9->gof.num_ref_pics[i]; ++p) {
      uint8_t p_diff;
      RETURN_FALSE_ON_ERROR(parser->ReadUInt8(&p_diff));
      vp9->gof.pid_diff[i][p] = p_diff;
    }
  }
  return true;
}

// Gets the size of next payload chunk to send. Returns 0 on error.
size_t CalcNextSize(size_t max_length, size_t rem_bytes) {
  if (max_length == 0 || rem_bytes == 0) {
    return 0;
  }
  if (kBalancedMode) {
    size_t num_frags = std::ceil(static_cast<double>(rem_bytes) / max_length);
    return static_cast<size_t>(
        static_cast<double>(rem_bytes) / num_frags + 0.5);
  }
  return max_length >= rem_bytes ? rem_bytes : max_length;
}
}  // namespace


RtpPacketizerVp9::RtpPacketizerVp9(const RTPVideoHeaderVP9& hdr,
                                   size_t max_payload_length)
    : hdr_(hdr),
      max_payload_length_(max_payload_length),
      payload_(nullptr),
      payload_size_(0) {
}

RtpPacketizerVp9::~RtpPacketizerVp9() {
}

ProtectionType RtpPacketizerVp9::GetProtectionType() {
  bool protect =
      hdr_.temporal_idx == 0 || hdr_.temporal_idx == kNoTemporalIdx;
  return protect ? kProtectedPacket : kUnprotectedPacket;
}

StorageType RtpPacketizerVp9::GetStorageType(uint32_t retransmission_settings) {
  StorageType storage = kAllowRetransmission;
  if (hdr_.temporal_idx == 0 &&
      !(retransmission_settings & kRetransmitBaseLayer)) {
    storage = kDontRetransmit;
  } else if (hdr_.temporal_idx != kNoTemporalIdx && hdr_.temporal_idx > 0 &&
             !(retransmission_settings & kRetransmitHigherLayers)) {
    storage = kDontRetransmit;
  }
  return storage;
}

std::string RtpPacketizerVp9::ToString() {
  return "RtpPacketizerVp9";
}

void RtpPacketizerVp9::SetPayloadData(
    const uint8_t* payload,
    size_t payload_size,
    const RTPFragmentationHeader* fragmentation) {
  payload_ = payload;
  payload_size_ = payload_size;
  GeneratePackets();
}

void RtpPacketizerVp9::GeneratePackets() {
  if (max_payload_length_ < PayloadDescriptorLength(hdr_) + 1) {
    LOG(LS_ERROR) << "Payload header and one payload byte won't fit.";
    return;
  }
  size_t bytes_processed = 0;
  while (bytes_processed < payload_size_) {
    size_t rem_bytes = payload_size_ - bytes_processed;
    size_t rem_payload_len = max_payload_length_ -
         (bytes_processed ? PayloadDescriptorLengthMinusSsData(hdr_)
                          : PayloadDescriptorLength(hdr_));

    size_t packet_bytes = CalcNextSize(rem_payload_len, rem_bytes);
    if (packet_bytes == 0) {
      LOG(LS_ERROR) << "Failed to generate VP9 packets.";
      while (!packets_.empty())
        packets_.pop();
      return;
    }
    QueuePacket(bytes_processed, packet_bytes, bytes_processed == 0,
                rem_bytes == packet_bytes, &packets_);
    bytes_processed += packet_bytes;
  }
  assert(bytes_processed == payload_size_);
}

bool RtpPacketizerVp9::NextPacket(uint8_t* buffer,
                                  size_t* bytes_to_send,
                                  bool* last_packet) {
  if (packets_.empty()) {
    return false;
  }
  PacketInfo packet_info = packets_.front();
  packets_.pop();

  if (!WriteHeaderAndPayload(packet_info, buffer, bytes_to_send)) {
    return false;
  }
  *last_packet =
      packets_.empty() && (hdr_.spatial_idx == kNoSpatialIdx ||
                           hdr_.spatial_idx == hdr_.num_spatial_layers - 1);
  return true;
}

// VP9 format:
//
// Payload descriptor for F = 1 (flexible mode)
//       0 1 2 3 4 5 6 7
//      +-+-+-+-+-+-+-+-+
//      |I|P|L|F|B|E|V|-| (REQUIRED)
//      +-+-+-+-+-+-+-+-+
// I:   |M| PICTURE ID  | (RECOMMENDED)
//      +-+-+-+-+-+-+-+-+
// M:   | EXTENDED PID  | (RECOMMENDED)
//      +-+-+-+-+-+-+-+-+
// L:   |  T  |U|  S  |D| (CONDITIONALLY RECOMMENDED)
//      +-+-+-+-+-+-+-+-+                             -|
// P,F: | P_DIFF    |X|N| (CONDITIONALLY RECOMMENDED)  .
//      +-+-+-+-+-+-+-+-+                              . up to 3 times
// X:   |EXTENDED P_DIFF|                              .
//      +-+-+-+-+-+-+-+-+                             -|
// V:   | SS            |
//      | ..            |
//      +-+-+-+-+-+-+-+-+
//
// Payload descriptor for F = 0 (non-flexible mode)
//       0 1 2 3 4 5 6 7
//      +-+-+-+-+-+-+-+-+
//      |I|P|L|F|B|E|V|-| (REQUIRED)
//      +-+-+-+-+-+-+-+-+
// I:   |M| PICTURE ID  | (RECOMMENDED)
//      +-+-+-+-+-+-+-+-+
// M:   | EXTENDED PID  | (RECOMMENDED)
//      +-+-+-+-+-+-+-+-+
// L:   |GOF_IDX|  S  |D| (CONDITIONALLY RECOMMENDED)
//      +-+-+-+-+-+-+-+-+
//      |   TL0PICIDX   | (CONDITIONALLY REQUIRED)
//      +-+-+-+-+-+-+-+-+
// V:   | SS            |
//      | ..            |
//      +-+-+-+-+-+-+-+-+

bool RtpPacketizerVp9::WriteHeaderAndPayload(const PacketInfo& packet_info,
                                             uint8_t* buffer,
                                             size_t* bytes_to_send) const {
  size_t header_length;
  if (!WriteHeader(packet_info, buffer, &header_length))
    return false;

  // Copy payload data.
  memcpy(&buffer[header_length],
         &payload_[packet_info.payload_start_pos], packet_info.size);

  *bytes_to_send = header_length + packet_info.size;
  return true;
}

bool RtpPacketizerVp9::WriteHeader(const PacketInfo& packet_info,
                                   uint8_t* buffer,
                                   size_t* header_length) const {
  // Required payload descriptor byte.
  bool i_bit = PictureIdPresent(hdr_);
  bool p_bit = hdr_.inter_pic_predicted;
  bool l_bit = LayerInfoPresent(hdr_);
  bool f_bit = hdr_.flexible_mode;
  bool b_bit = packet_info.layer_begin;
  bool e_bit = packet_info.layer_end;
  bool v_bit = hdr_.ss_data_available && b_bit;

  rtc::BitBufferWriter writer(buffer, max_payload_length_);
  RETURN_FALSE_ON_ERROR(writer.WriteBits(i_bit ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer.WriteBits(p_bit ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer.WriteBits(l_bit ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer.WriteBits(f_bit ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer.WriteBits(b_bit ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer.WriteBits(e_bit ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer.WriteBits(v_bit ? 1 : 0, 1));
  RETURN_FALSE_ON_ERROR(writer.WriteBits(kReservedBitValue0, 1));

  // Add fields that are present.
  if (i_bit && !WritePictureId(hdr_, &writer)) {
    LOG(LS_ERROR) << "Failed writing VP9 picture id.";
    return false;
  }
  if (l_bit && !WriteLayerInfo(hdr_, &writer)) {
    LOG(LS_ERROR) << "Failed writing VP9 layer info.";
    return false;
  }
  if (p_bit && f_bit && !WriteRefIndices(hdr_, &writer)) {
    LOG(LS_ERROR) << "Failed writing VP9 ref indices.";
    return false;
  }
  if (v_bit && !WriteSsData(hdr_, &writer)) {
    LOG(LS_ERROR) << "Failed writing VP9 SS data.";
    return false;
  }

  size_t offset_bytes = 0;
  size_t offset_bits = 0;
  writer.GetCurrentOffset(&offset_bytes, &offset_bits);
  assert(offset_bits == 0);

  *header_length = offset_bytes;
  return true;
}

bool RtpDepacketizerVp9::Parse(ParsedPayload* parsed_payload,
                               const uint8_t* payload,
                               size_t payload_length) {
  assert(parsed_payload != nullptr);
  if (payload_length == 0) {
    LOG(LS_ERROR) << "Payload length is zero.";
    return false;
  }

  // Parse mandatory first byte of payload descriptor.
  rtc::BitBuffer parser(payload, payload_length);
  uint32_t i_bit, p_bit, l_bit, f_bit, b_bit, e_bit, v_bit;
  RETURN_FALSE_ON_ERROR(parser.ReadBits(&i_bit, 1));
  RETURN_FALSE_ON_ERROR(parser.ReadBits(&p_bit, 1));
  RETURN_FALSE_ON_ERROR(parser.ReadBits(&l_bit, 1));
  RETURN_FALSE_ON_ERROR(parser.ReadBits(&f_bit, 1));
  RETURN_FALSE_ON_ERROR(parser.ReadBits(&b_bit, 1));
  RETURN_FALSE_ON_ERROR(parser.ReadBits(&e_bit, 1));
  RETURN_FALSE_ON_ERROR(parser.ReadBits(&v_bit, 1));
  RETURN_FALSE_ON_ERROR(parser.ConsumeBits(1));

  // Parsed payload.
  parsed_payload->type.Video.width = 0;
  parsed_payload->type.Video.height = 0;
  parsed_payload->type.Video.simulcastIdx = 0;
  parsed_payload->type.Video.codec = kRtpVideoVp9;

  parsed_payload->frame_type = p_bit ? kVideoFrameDelta : kVideoFrameKey;

  RTPVideoHeaderVP9* vp9 = &parsed_payload->type.Video.codecHeader.VP9;
  vp9->InitRTPVideoHeaderVP9();
  vp9->inter_pic_predicted = p_bit ? true : false;
  vp9->flexible_mode = f_bit ? true : false;
  vp9->beginning_of_frame = b_bit ? true : false;
  vp9->end_of_frame = e_bit ? true : false;
  vp9->ss_data_available = v_bit ? true : false;
  vp9->spatial_idx = 0;

  // Parse fields that are present.
  if (i_bit && !ParsePictureId(&parser, vp9)) {
    LOG(LS_ERROR) << "Failed parsing VP9 picture id.";
    return false;
  }
  if (l_bit && !ParseLayerInfo(&parser, vp9)) {
    LOG(LS_ERROR) << "Failed parsing VP9 layer info.";
    return false;
  }
  if (p_bit && f_bit && !ParseRefIndices(&parser, vp9)) {
    LOG(LS_ERROR) << "Failed parsing VP9 ref indices.";
    return false;
  }
  if (v_bit) {
    if (!ParseSsData(&parser, vp9)) {
      LOG(LS_ERROR) << "Failed parsing VP9 SS data.";
      return false;
    }
    if (vp9->spatial_layer_resolution_present) {
      // TODO(asapersson): Add support for spatial layers.
      parsed_payload->type.Video.width = vp9->width[0];
      parsed_payload->type.Video.height = vp9->height[0];
    }
  }
  parsed_payload->type.Video.isFirstPacket = b_bit && (vp9->spatial_idx == 0);

  uint64_t rem_bits = parser.RemainingBitCount();
  assert(rem_bits % 8 == 0);
  parsed_payload->payload_length = rem_bits / 8;
  if (parsed_payload->payload_length == 0) {
    LOG(LS_ERROR) << "Failed parsing VP9 payload data.";
    return false;
  }
  parsed_payload->payload =
      payload + payload_length - parsed_payload->payload_length;

  return true;
}
}  // namespace webrtc
