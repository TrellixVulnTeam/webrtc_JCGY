/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vpx_mem/vpx_mem.h"
#include "vpx_ports/mem.h"

#include "vp9/common/vp9_alloccommon.h"
#include "vp9/encoder/vp9_aq_cyclicrefresh.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_entropymode.h"
#include "vp9/common/vp9_quant_common.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_systemdependent.h"

#include "vp9/encoder/vp9_encodemv.h"
#include "vp9/encoder/vp9_ratectrl.h"

// Max rate target for 1080P and below encodes under normal circumstances
// (1920 * 1080 / (16 * 16)) * MAX_MB_RATE bits per MB
#define MAX_MB_RATE 250
#define MAXRATE_1080P 2025000

#define DEFAULT_KF_BOOST 2000
#define DEFAULT_GF_BOOST 2000

#define LIMIT_QRANGE_FOR_ALTREF_AND_KEY 1

#define MIN_BPB_FACTOR 0.005
#define MAX_BPB_FACTOR 50

#define FRAME_OVERHEAD_BITS 200

#if CONFIG_VP9_HIGHBITDEPTH
#define ASSIGN_MINQ_TABLE(bit_depth, name) \
  do { \
    switch (bit_depth) { \
      case VPX_BITS_8: \
        name = name##_8; \
        break; \
      case VPX_BITS_10: \
        name = name##_10; \
        break; \
      case VPX_BITS_12: \
        name = name##_12; \
        break; \
      default: \
        assert(0 && "bit_depth should be VPX_BITS_8, VPX_BITS_10" \
                    " or VPX_BITS_12"); \
        name = NULL; \
    } \
  } while (0)
#else
#define ASSIGN_MINQ_TABLE(bit_depth, name) \
  do { \
    (void) bit_depth; \
    name = name##_8; \
  } while (0)
#endif

// Tables relating active max Q to active min Q
static int kf_low_motion_minq_8[QINDEX_RANGE];
static int kf_high_motion_minq_8[QINDEX_RANGE];
static int arfgf_low_motion_minq_8[QINDEX_RANGE];
static int arfgf_high_motion_minq_8[QINDEX_RANGE];
static int inter_minq_8[QINDEX_RANGE];
static int rtc_minq_8[QINDEX_RANGE];

#if CONFIG_VP9_HIGHBITDEPTH
static int kf_low_motion_minq_10[QINDEX_RANGE];
static int kf_high_motion_minq_10[QINDEX_RANGE];
static int arfgf_low_motion_minq_10[QINDEX_RANGE];
static int arfgf_high_motion_minq_10[QINDEX_RANGE];
static int inter_minq_10[QINDEX_RANGE];
static int rtc_minq_10[QINDEX_RANGE];
static int kf_low_motion_minq_12[QINDEX_RANGE];
static int kf_high_motion_minq_12[QINDEX_RANGE];
static int arfgf_low_motion_minq_12[QINDEX_RANGE];
static int arfgf_high_motion_minq_12[QINDEX_RANGE];
static int inter_minq_12[QINDEX_RANGE];
static int rtc_minq_12[QINDEX_RANGE];
#endif

static int gf_high = 2000;
static int gf_low = 400;
static int kf_high = 5000;
static int kf_low = 400;

// Functions to compute the active minq lookup table entries based on a
// formulaic approach to facilitate easier adjustment of the Q tables.
// The formulae were derived from computing a 3rd order polynomial best
// fit to the original data (after plotting real maxq vs minq (not q index))
static int get_minq_index(double maxq, double x3, double x2, double x1,
                          vpx_bit_depth_t bit_depth) {
  int i;
  const double minqtarget = MIN(((x3 * maxq + x2) * maxq + x1) * maxq,
                                maxq);

  // Special case handling to deal with the step from q2.0
  // down to lossless mode represented by q 1.0.
  if (minqtarget <= 2.0)
    return 0;

  for (i = 0; i < QINDEX_RANGE; i++) {
    if (minqtarget <= vp9_convert_qindex_to_q(i, bit_depth))
      return i;
  }

  return QINDEX_RANGE - 1;
}

static void init_minq_luts(int *kf_low_m, int *kf_high_m,
                           int *arfgf_low, int *arfgf_high,
                           int *inter, int *rtc, vpx_bit_depth_t bit_depth) {
  int i;
  for (i = 0; i < QINDEX_RANGE; i++) {
    const double maxq = vp9_convert_qindex_to_q(i, bit_depth);
    kf_low_m[i] = get_minq_index(maxq, 0.000001, -0.0004, 0.150, bit_depth);
    kf_high_m[i] = get_minq_index(maxq, 0.0000021, -0.00125, 0.55, bit_depth);
    arfgf_low[i] = get_minq_index(maxq, 0.0000015, -0.0009, 0.30, bit_depth);
    arfgf_high[i] = get_minq_index(maxq, 0.0000021, -0.00125, 0.55, bit_depth);
    inter[i] = get_minq_index(maxq, 0.00000271, -0.00113, 0.90, bit_depth);
    rtc[i] = get_minq_index(maxq, 0.00000271, -0.00113, 0.70, bit_depth);
  }
}

void vp9_rc_init_minq_luts(void) {
  init_minq_luts(kf_low_motion_minq_8, kf_high_motion_minq_8,
                 arfgf_low_motion_minq_8, arfgf_high_motion_minq_8,
                 inter_minq_8, rtc_minq_8, VPX_BITS_8);
#if CONFIG_VP9_HIGHBITDEPTH
  init_minq_luts(kf_low_motion_minq_10, kf_high_motion_minq_10,
                 arfgf_low_motion_minq_10, arfgf_high_motion_minq_10,
                 inter_minq_10, rtc_minq_10, VPX_BITS_10);
  init_minq_luts(kf_low_motion_minq_12, kf_high_motion_minq_12,
                 arfgf_low_motion_minq_12, arfgf_high_motion_minq_12,
                 inter_minq_12, rtc_minq_12, VPX_BITS_12);
#endif
}

// These functions use formulaic calculations to make playing with the
// quantizer tables easier. If necessary they can be replaced by lookup
// tables if and when things settle down in the experimental bitstream
double vp9_convert_qindex_to_q(int qindex, vpx_bit_depth_t bit_depth) {
  // Convert the index to a real Q value (scaled down to match old Q values)
#if CONFIG_VP9_HIGHBITDEPTH
  switch (bit_depth) {
    case VPX_BITS_8:
      return vp9_ac_quant(qindex, 0, bit_depth) / 4.0;
    case VPX_BITS_10:
      return vp9_ac_quant(qindex, 0, bit_depth) / 16.0;
    case VPX_BITS_12:
      return vp9_ac_quant(qindex, 0, bit_depth) / 64.0;
    default:
      assert(0 && "bit_depth should be VPX_BITS_8, VPX_BITS_10 or VPX_BITS_12");
      return -1.0;
  }
#else
  return vp9_ac_quant(qindex, 0, bit_depth) / 4.0;
#endif
}

int vp9_rc_bits_per_mb(FRAME_TYPE frame_type, int qindex,
                       double correction_factor,
                       vpx_bit_depth_t bit_depth) {
  const double q = vp9_convert_qindex_to_q(qindex, bit_depth);
  int enumerator = frame_type == KEY_FRAME ? 2700000 : 1800000;

  assert(correction_factor <= MAX_BPB_FACTOR &&
         correction_factor >= MIN_BPB_FACTOR);

  // q based adjustment to baseline enumerator
  enumerator += (int)(enumerator * q) >> 12;
  return (int)(enumerator * correction_factor / q);
}

int vp9_estimate_bits_at_q(FRAME_TYPE frame_type, int q, int mbs,
                           double correction_factor,
                           vpx_bit_depth_t bit_depth) {
  const int bpm = (int)(vp9_rc_bits_per_mb(frame_type, q, correction_factor,
                                           bit_depth));
  return MAX(FRAME_OVERHEAD_BITS,
             (int)((uint64_t)bpm * mbs) >> BPER_MB_NORMBITS);
}

int vp9_rc_clamp_pframe_target_size(const VP9_COMP *const cpi, int target) {
  const RATE_CONTROL *rc = &cpi->rc;
  const VP9EncoderConfig *oxcf = &cpi->oxcf;
  const int min_frame_target = MAX(rc->min_frame_bandwidth,
                                   rc->avg_frame_bandwidth >> 5);
  if (target < min_frame_target)
    target = min_frame_target;
  if (cpi->refresh_golden_frame && rc->is_src_frame_alt_ref) {
    // If there is an active ARF at this location use the minimum
    // bits on this frame even if it is a constructed arf.
    // The active maximum quantizer insures that an appropriate
    // number of bits will be spent if needed for constructed ARFs.
    target = min_frame_target;
  }
  // Clip the frame target to the maximum allowed value.
  if (target > rc->max_frame_bandwidth)
    target = rc->max_frame_bandwidth;
  if (oxcf->rc_max_inter_bitrate_pct) {
    const int max_rate = rc->avg_frame_bandwidth *
                         oxcf->rc_max_inter_bitrate_pct / 100;
    target = MIN(target, max_rate);
  }
  return target;
}

int vp9_rc_clamp_iframe_target_size(const VP9_COMP *const cpi, int target) {
  const RATE_CONTROL *rc = &cpi->rc;
  const VP9EncoderConfig *oxcf = &cpi->oxcf;
  if (oxcf->rc_max_intra_bitrate_pct) {
    const int max_rate = rc->avg_frame_bandwidth *
                             oxcf->rc_max_intra_bitrate_pct / 100;
    target = MIN(target, max_rate);
  }
  if (target > rc->max_frame_bandwidth)
    target = rc->max_frame_bandwidth;
  return target;
}

// Update the buffer level for higher temporal layers, given the encoded current
// temporal layer.
static void update_layer_buffer_level(SVC *svc, int encoded_frame_size) {
  int i = 0;
  int current_temporal_layer = svc->temporal_layer_id;
  for (i = current_temporal_layer + 1;
      i < svc->number_temporal_layers; ++i) {
    const int layer = LAYER_IDS_TO_IDX(svc->spatial_layer_id, i,
                                       svc->number_temporal_layers);
    LAYER_CONTEXT *lc = &svc->layer_context[layer];
    RATE_CONTROL *lrc = &lc->rc;
    int bits_off_for_this_layer = (int)(lc->target_bandwidth / lc->framerate -
        encoded_frame_size);
    lrc->bits_off_target += bits_off_for_this_layer;

    // Clip buffer level to maximum buffer size for the layer.
    lrc->bits_off_target = MIN(lrc->bits_off_target, lrc->maximum_buffer_size);
    lrc->buffer_level = lrc->bits_off_target;
  }
}

// Update the buffer level: leaky bucket model.
static void update_buffer_level(VP9_COMP *cpi, int encoded_frame_size) {
  const VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;

  // Non-viewable frames are a special case and are treated as pure overhead.
  if (!cm->show_frame) {
    rc->bits_off_target -= encoded_frame_size;
  } else {
    rc->bits_off_target += rc->avg_frame_bandwidth - encoded_frame_size;
  }

  // Clip the buffer level to the maximum specified buffer size.
  rc->bits_off_target = MIN(rc->bits_off_target, rc->maximum_buffer_size);
  rc->buffer_level = rc->bits_off_target;

  if (is_one_pass_cbr_svc(cpi)) {
    update_layer_buffer_level(&cpi->svc, encoded_frame_size);
  }
}

int vp9_rc_get_default_min_gf_interval(
    int width, int height, double framerate) {
  // Assume we do not need any constraint lower than 4K 20 fps
  static const double factor_safe = 3840 * 2160 * 20.0;
  const double factor = width * height * framerate;
  const int default_interval =
      clamp((int)(framerate * 0.125), MIN_GF_INTERVAL, MAX_GF_INTERVAL);

  if (factor <= factor_safe)
    return default_interval;
  else
    return MAX(default_interval,
               (int)(MIN_GF_INTERVAL * factor / factor_safe + 0.5));
  // Note this logic makes:
  // 4K24: 5
  // 4K30: 6
  // 4K60: 12
}

int vp9_rc_get_default_max_gf_interval(double framerate, int min_gf_interval) {
  int interval = MIN(MAX_GF_INTERVAL, (int)(framerate * 0.75));
  interval += (interval & 0x01);  // Round to even value
  return MAX(interval, min_gf_interval);
}

void vp9_rc_init(const VP9EncoderConfig *oxcf, int pass, RATE_CONTROL *rc) {
  int i;

  if (pass == 0 && oxcf->rc_mode == VPX_CBR) {
    rc->avg_frame_qindex[KEY_FRAME] = oxcf->worst_allowed_q;
    rc->avg_frame_qindex[INTER_FRAME] = oxcf->worst_allowed_q;
  } else {
    rc->avg_frame_qindex[KEY_FRAME] = (oxcf->worst_allowed_q +
                                       oxcf->best_allowed_q) / 2;
    rc->avg_frame_qindex[INTER_FRAME] = (oxcf->worst_allowed_q +
                                         oxcf->best_allowed_q) / 2;
  }

  rc->last_q[KEY_FRAME] = oxcf->best_allowed_q;
  rc->last_q[INTER_FRAME] = oxcf->worst_allowed_q;

  rc->buffer_level =    rc->starting_buffer_level;
  rc->bits_off_target = rc->starting_buffer_level;

  rc->rolling_target_bits      = rc->avg_frame_bandwidth;
  rc->rolling_actual_bits      = rc->avg_frame_bandwidth;
  rc->long_rolling_target_bits = rc->avg_frame_bandwidth;
  rc->long_rolling_actual_bits = rc->avg_frame_bandwidth;

  rc->total_actual_bits = 0;
  rc->total_target_bits = 0;
  rc->total_target_vs_actual = 0;

  rc->frames_since_key = 8;  // Sensible default for first frame.
  rc->this_key_frame_forced = 0;
  rc->next_key_frame_forced = 0;
  rc->source_alt_ref_pending = 0;
  rc->source_alt_ref_active = 0;

  rc->frames_till_gf_update_due = 0;
  rc->ni_av_qi = oxcf->worst_allowed_q;
  rc->ni_tot_qi = 0;
  rc->ni_frames = 0;

  rc->tot_q = 0.0;
  rc->avg_q = vp9_convert_qindex_to_q(oxcf->worst_allowed_q, oxcf->bit_depth);

  for (i = 0; i < RATE_FACTOR_LEVELS; ++i) {
    rc->rate_correction_factors[i] = 1.0;
  }

  rc->min_gf_interval = oxcf->min_gf_interval;
  rc->max_gf_interval = oxcf->max_gf_interval;
  if (rc->min_gf_interval == 0)
    rc->min_gf_interval = vp9_rc_get_default_min_gf_interval(
        oxcf->width, oxcf->height, oxcf->init_framerate);
  if (rc->max_gf_interval == 0)
    rc->max_gf_interval = vp9_rc_get_default_max_gf_interval(
        oxcf->init_framerate, rc->min_gf_interval);
  rc->baseline_gf_interval = (rc->min_gf_interval + rc->max_gf_interval) / 2;
}

int vp9_rc_drop_frame(VP9_COMP *cpi) {
  const VP9EncoderConfig *oxcf = &cpi->oxcf;
  RATE_CONTROL *const rc = &cpi->rc;

  if (!oxcf->drop_frames_water_mark) {
    return 0;
  } else {
    if (rc->buffer_level < 0) {
      // Always drop if buffer is below 0.
      return 1;
    } else {
      // If buffer is below drop_mark, for now just drop every other frame
      // (starting with the next frame) until it increases back over drop_mark.
      int drop_mark = (int)(oxcf->drop_frames_water_mark *
          rc->optimal_buffer_level / 100);
      if ((rc->buffer_level > drop_mark) &&
          (rc->decimation_factor > 0)) {
        --rc->decimation_factor;
      } else if (rc->buffer_level <= drop_mark &&
          rc->decimation_factor == 0) {
        rc->decimation_factor = 1;
      }
      if (rc->decimation_factor > 0) {
        if (rc->decimation_count > 0) {
          --rc->decimation_count;
          return 1;
        } else {
          rc->decimation_count = rc->decimation_factor;
          return 0;
        }
      } else {
        rc->decimation_count = 0;
        return 0;
      }
    }
  }
}

static double get_rate_correction_factor(const VP9_COMP *cpi) {
  const RATE_CONTROL *const rc = &cpi->rc;
  double rcf;

  if (cpi->common.frame_type == KEY_FRAME) {
    rcf = rc->rate_correction_factors[KF_STD];
  } else if (cpi->oxcf.pass == 2) {
    RATE_FACTOR_LEVEL rf_lvl =
      cpi->twopass.gf_group.rf_level[cpi->twopass.gf_group.index];
    rcf = rc->rate_correction_factors[rf_lvl];
  } else {
    if ((cpi->refresh_alt_ref_frame || cpi->refresh_golden_frame) &&
        !rc->is_src_frame_alt_ref && !cpi->use_svc &&
        (cpi->oxcf.rc_mode != VPX_CBR || cpi->oxcf.gf_cbr_boost_pct > 20))
      rcf = rc->rate_correction_factors[GF_ARF_STD];
    else
      rcf = rc->rate_correction_factors[INTER_NORMAL];
  }
  rcf *= rcf_mult[rc->frame_size_selector];
  return fclamp(rcf, MIN_BPB_FACTOR, MAX_BPB_FACTOR);
}

static void set_rate_correction_factor(VP9_COMP *cpi, double factor) {
  RATE_CONTROL *const rc = &cpi->rc;

  // Normalize RCF to account for the size-dependent scaling factor.
  factor /= rcf_mult[cpi->rc.frame_size_selector];

  factor = fclamp(factor, MIN_BPB_FACTOR, MAX_BPB_FACTOR);

  if (cpi->common.frame_type == KEY_FRAME) {
    rc->rate_correction_factors[KF_STD] = factor;
  } else if (cpi->oxcf.pass == 2) {
    RATE_FACTOR_LEVEL rf_lvl =
      cpi->twopass.gf_group.rf_level[cpi->twopass.gf_group.index];
    rc->rate_correction_factors[rf_lvl] = factor;
  } else {
    if ((cpi->refresh_alt_ref_frame || cpi->refresh_golden_frame) &&
        !rc->is_src_frame_alt_ref && !cpi->use_svc &&
        (cpi->oxcf.rc_mode != VPX_CBR || cpi->oxcf.gf_cbr_boost_pct > 20))
      rc->rate_correction_factors[GF_ARF_STD] = factor;
    else
      rc->rate_correction_factors[INTER_NORMAL] = factor;
  }
}

void vp9_rc_update_rate_correction_factors(VP9_COMP *cpi) {
  const VP9_COMMON *const cm = &cpi->common;
  int correction_factor = 100;
  double rate_correction_factor = get_rate_correction_factor(cpi);
  double adjustment_limit;

  int projected_size_based_on_q = 0;

  // Do not update the rate factors for arf overlay frames.
  if (cpi->rc.is_src_frame_alt_ref)
    return;

  // Clear down mmx registers to allow floating point in what follows
  vp9_clear_system_state();

  // Work out how big we would have expected the frame to be at this Q given
  // the current correction factor.
  // Stay in double to avoid int overflow when values are large
  if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ && cpi->common.seg.enabled) {
    projected_size_based_on_q =
        vp9_cyclic_refresh_estimate_bits_at_q(cpi, rate_correction_factor);
  } else {
    projected_size_based_on_q = vp9_estimate_bits_at_q(cpi->common.frame_type,
                                                       cm->base_qindex,
                                                       cm->MBs,
                                                       rate_correction_factor,
                                                       cm->bit_depth);
  }
  // Work out a size correction factor.
  if (projected_size_based_on_q > FRAME_OVERHEAD_BITS)
    correction_factor = (int)((100 * (int64_t)cpi->rc.projected_frame_size) /
                        projected_size_based_on_q);

  // More heavily damped adjustment used if we have been oscillating either side
  // of target.
  adjustment_limit = 0.25 +
      0.5 * MIN(1, fabs(log10(0.01 * correction_factor)));

  cpi->rc.q_2_frame = cpi->rc.q_1_frame;
  cpi->rc.q_1_frame = cm->base_qindex;
  cpi->rc.rc_2_frame = cpi->rc.rc_1_frame;
  if (correction_factor > 110)
    cpi->rc.rc_1_frame = -1;
  else if (correction_factor < 90)
    cpi->rc.rc_1_frame = 1;
  else
    cpi->rc.rc_1_frame = 0;

  if (correction_factor > 102) {
    // We are not already at the worst allowable quality
    correction_factor = (int)(100 + ((correction_factor - 100) *
                                  adjustment_limit));
    rate_correction_factor = (rate_correction_factor * correction_factor) / 100;
    // Keep rate_correction_factor within limits
    if (rate_correction_factor > MAX_BPB_FACTOR)
      rate_correction_factor = MAX_BPB_FACTOR;
  } else if (correction_factor < 99) {
    // We are not already at the best allowable quality
    correction_factor = (int)(100 - ((100 - correction_factor) *
                                  adjustment_limit));
    rate_correction_factor = (rate_correction_factor * correction_factor) / 100;

    // Keep rate_correction_factor within limits
    if (rate_correction_factor < MIN_BPB_FACTOR)
      rate_correction_factor = MIN_BPB_FACTOR;
  }

  set_rate_correction_factor(cpi, rate_correction_factor);
}


int vp9_rc_regulate_q(const VP9_COMP *cpi, int target_bits_per_frame,
                      int active_best_quality, int active_worst_quality) {
  const VP9_COMMON *const cm = &cpi->common;
  int q = active_worst_quality;
  int last_error = INT_MAX;
  int i, target_bits_per_mb, bits_per_mb_at_this_q;
  const double correction_factor = get_rate_correction_factor(cpi);

  // Calculate required scaling factor based on target frame size and size of
  // frame produced using previous Q.
  target_bits_per_mb =
      ((uint64_t)target_bits_per_frame << BPER_MB_NORMBITS) / cm->MBs;

  i = active_best_quality;

  do {
    if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ &&
        cm->seg.enabled &&
        cpi->svc.temporal_layer_id == 0 &&
        cpi->svc.spatial_layer_id == 0) {
      bits_per_mb_at_this_q =
          (int)vp9_cyclic_refresh_rc_bits_per_mb(cpi, i, correction_factor);
    } else {
      bits_per_mb_at_this_q = (int)vp9_rc_bits_per_mb(cm->frame_type, i,
                                                      correction_factor,
                                                      cm->bit_depth);
    }

    if (bits_per_mb_at_this_q <= target_bits_per_mb) {
      if ((target_bits_per_mb - bits_per_mb_at_this_q) <= last_error)
        q = i;
      else
        q = i - 1;

      break;
    } else {
      last_error = bits_per_mb_at_this_q - target_bits_per_mb;
    }
  } while (++i <= active_worst_quality);

  // In CBR mode, this makes sure q is between oscillating Qs to prevent
  // resonance.
  if (cpi->oxcf.rc_mode == VPX_CBR &&
      (cpi->rc.rc_1_frame * cpi->rc.rc_2_frame == -1) &&
      cpi->rc.q_1_frame != cpi->rc.q_2_frame) {
    q = clamp(q, MIN(cpi->rc.q_1_frame, cpi->rc.q_2_frame),
              MAX(cpi->rc.q_1_frame, cpi->rc.q_2_frame));
  }
  return q;
}

static int get_active_quality(int q, int gfu_boost, int low, int high,
                              int *low_motion_minq, int *high_motion_minq) {
  if (gfu_boost > high) {
    return low_motion_minq[q];
  } else if (gfu_boost < low) {
    return high_motion_minq[q];
  } else {
    const int gap = high - low;
    const int offset = high - gfu_boost;
    const int qdiff = high_motion_minq[q] - low_motion_minq[q];
    const int adjustment = ((offset * qdiff) + (gap >> 1)) / gap;
    return low_motion_minq[q] + adjustment;
  }
}

static int get_kf_active_quality(const RATE_CONTROL *const rc, int q,
                                 vpx_bit_depth_t bit_depth) {
  int *kf_low_motion_minq;
  int *kf_high_motion_minq;
  ASSIGN_MINQ_TABLE(bit_depth, kf_low_motion_minq);
  ASSIGN_MINQ_TABLE(bit_depth, kf_high_motion_minq);
  return get_active_quality(q, rc->kf_boost, kf_low, kf_high,
                            kf_low_motion_minq, kf_high_motion_minq);
}

static int get_gf_active_quality(const RATE_CONTROL *const rc, int q,
                                 vpx_bit_depth_t bit_depth) {
  int *arfgf_low_motion_minq;
  int *arfgf_high_motion_minq;
  ASSIGN_MINQ_TABLE(bit_depth, arfgf_low_motion_minq);
  ASSIGN_MINQ_TABLE(bit_depth, arfgf_high_motion_minq);
  return get_active_quality(q, rc->gfu_boost, gf_low, gf_high,
                            arfgf_low_motion_minq, arfgf_high_motion_minq);
}

static int calc_active_worst_quality_one_pass_vbr(const VP9_COMP *cpi) {
  const RATE_CONTROL *const rc = &cpi->rc;
  const unsigned int curr_frame = cpi->common.current_video_frame;
  int active_worst_quality;

  if (cpi->common.frame_type == KEY_FRAME) {
    active_worst_quality = curr_frame == 0 ? rc->worst_quality
                                           : rc->last_q[KEY_FRAME] * 2;
  } else {
    if (!rc->is_src_frame_alt_ref &&
        (cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame)) {
      active_worst_quality =  curr_frame == 1 ? rc->last_q[KEY_FRAME] * 5 / 4
                                              : rc->last_q[INTER_FRAME];
    } else {
      active_worst_quality = curr_frame == 1 ? rc->last_q[KEY_FRAME] * 2
                                             : rc->last_q[INTER_FRAME] * 2;
    }
  }
  return MIN(active_worst_quality, rc->worst_quality);
}

// Adjust active_worst_quality level based on buffer level.
static int calc_active_worst_quality_one_pass_cbr(const VP9_COMP *cpi) {
  // Adjust active_worst_quality: If buffer is above the optimal/target level,
  // bring active_worst_quality down depending on fullness of buffer.
  // If buffer is below the optimal level, let the active_worst_quality go from
  // ambient Q (at buffer = optimal level) to worst_quality level
  // (at buffer = critical level).
  const VP9_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *rc = &cpi->rc;
  // Buffer level below which we push active_worst to worst_quality.
  int64_t critical_level = rc->optimal_buffer_level >> 3;
  int64_t buff_lvl_step = 0;
  int adjustment = 0;
  int active_worst_quality;
  int ambient_qp;
  if (cm->frame_type == KEY_FRAME)
    return rc->worst_quality;
  // For ambient_qp we use minimum of avg_frame_qindex[KEY_FRAME/INTER_FRAME]
  // for the first few frames following key frame. These are both initialized
  // to worst_quality and updated with (3/4, 1/4) average in postencode_update.
  // So for first few frames following key, the qp of that key frame is weighted
  // into the active_worst_quality setting.
  ambient_qp = (cm->current_video_frame < 5) ?
      MIN(rc->avg_frame_qindex[INTER_FRAME], rc->avg_frame_qindex[KEY_FRAME]) :
      rc->avg_frame_qindex[INTER_FRAME];
  active_worst_quality = MIN(rc->worst_quality,
                             ambient_qp * 5 / 4);
  if (rc->buffer_level > rc->optimal_buffer_level) {
    // Adjust down.
    // Maximum limit for down adjustment, ~30%.
    int max_adjustment_down = active_worst_quality / 3;
    if (max_adjustment_down) {
      buff_lvl_step = ((rc->maximum_buffer_size -
                        rc->optimal_buffer_level) / max_adjustment_down);
      if (buff_lvl_step)
        adjustment = (int)((rc->buffer_level - rc->optimal_buffer_level) /
                            buff_lvl_step);
      active_worst_quality -= adjustment;
    }
  } else if (rc->buffer_level > critical_level) {
    // Adjust up from ambient Q.
    if (critical_level) {
      buff_lvl_step = (rc->optimal_buffer_level - critical_level);
      if (buff_lvl_step) {
        adjustment = (int)((rc->worst_quality - ambient_qp) *
                           (rc->optimal_buffer_level - rc->buffer_level) /
                           buff_lvl_step);
      }
      active_worst_quality = ambient_qp + adjustment;
    }
  } else {
    // Set to worst_quality if buffer is below critical level.
    active_worst_quality = rc->worst_quality;
  }
  return active_worst_quality;
}

static int rc_pick_q_and_bounds_one_pass_cbr(const VP9_COMP *cpi,
                                             int *bottom_index,
                                             int *top_index) {
  const VP9_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  int active_best_quality;
  int active_worst_quality = calc_active_worst_quality_one_pass_cbr(cpi);
  int q;
  int *rtc_minq;
  ASSIGN_MINQ_TABLE(cm->bit_depth, rtc_minq);

  if (frame_is_intra_only(cm)) {
    active_best_quality = rc->best_quality;
    // Handle the special case for key frames forced when we have reached
    // the maximum key frame interval. Here force the Q to a range
    // based on the ambient Q to reduce the risk of popping.
    if (rc->this_key_frame_forced) {
      int qindex = rc->last_boosted_qindex;
      double last_boosted_q = vp9_convert_qindex_to_q(qindex, cm->bit_depth);
      int delta_qindex = vp9_compute_qdelta(rc, last_boosted_q,
                                            (last_boosted_q * 0.75),
                                            cm->bit_depth);
      active_best_quality = MAX(qindex + delta_qindex, rc->best_quality);
    } else if (cm->current_video_frame > 0) {
      // not first frame of one pass and kf_boost is set
      double q_adj_factor = 1.0;
      double q_val;

      active_best_quality =
          get_kf_active_quality(rc, rc->avg_frame_qindex[KEY_FRAME],
                                cm->bit_depth);

      // Allow somewhat lower kf minq with small image formats.
      if ((cm->width * cm->height) <= (352 * 288)) {
        q_adj_factor -= 0.25;
      }

      // Convert the adjustment factor to a qindex delta
      // on active_best_quality.
      q_val = vp9_convert_qindex_to_q(active_best_quality, cm->bit_depth);
      active_best_quality += vp9_compute_qdelta(rc, q_val,
                                                q_val * q_adj_factor,
                                                cm->bit_depth);
    }
  } else if (!rc->is_src_frame_alt_ref &&
             !cpi->use_svc &&
             (cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame)) {
    // Use the lower of active_worst_quality and recent
    // average Q as basis for GF/ARF best Q limit unless last frame was
    // a key frame.
    if (rc->frames_since_key > 1 &&
        rc->avg_frame_qindex[INTER_FRAME] < active_worst_quality) {
      q = rc->avg_frame_qindex[INTER_FRAME];
    } else {
      q = active_worst_quality;
    }
    active_best_quality = get_gf_active_quality(rc, q, cm->bit_depth);
  } else {
    // Use the lower of active_worst_quality and recent/average Q.
    if (cm->current_video_frame > 1) {
      if (rc->avg_frame_qindex[INTER_FRAME] < active_worst_quality)
        active_best_quality = rtc_minq[rc->avg_frame_qindex[INTER_FRAME]];
      else
        active_best_quality = rtc_minq[active_worst_quality];
    } else {
      if (rc->avg_frame_qindex[KEY_FRAME] < active_worst_quality)
        active_best_quality = rtc_minq[rc->avg_frame_qindex[KEY_FRAME]];
      else
        active_best_quality = rtc_minq[active_worst_quality];
    }
  }

  // Clip the active best and worst quality values to limits
  active_best_quality = clamp(active_best_quality,
                              rc->best_quality, rc->worst_quality);
  active_worst_quality = clamp(active_worst_quality,
                               active_best_quality, rc->worst_quality);

  *top_index = active_worst_quality;
  *bottom_index = active_best_quality;

#if LIMIT_QRANGE_FOR_ALTREF_AND_KEY
  // Limit Q range for the adaptive loop.
  if (cm->frame_type == KEY_FRAME &&
      !rc->this_key_frame_forced  &&
      !(cm->current_video_frame == 0)) {
    int qdelta = 0;
    vp9_clear_system_state();
    qdelta = vp9_compute_qdelta_by_rate(&cpi->rc, cm->frame_type,
                                        active_worst_quality, 2.0,
                                        cm->bit_depth);
    *top_index = active_worst_quality + qdelta;
    *top_index = (*top_index > *bottom_index) ? *top_index : *bottom_index;
  }
#endif

  // Special case code to try and match quality with forced key frames
  if (cm->frame_type == KEY_FRAME && rc->this_key_frame_forced) {
    q = rc->last_boosted_qindex;
  } else {
    q = vp9_rc_regulate_q(cpi, rc->this_frame_target,
                          active_best_quality, active_worst_quality);
    if (q > *top_index) {
      // Special case when we are targeting the max allowed rate
      if (rc->this_frame_target >= rc->max_frame_bandwidth)
        *top_index = q;
      else
        q = *top_index;
    }
  }
  assert(*top_index <= rc->worst_quality &&
         *top_index >= rc->best_quality);
  assert(*bottom_index <= rc->worst_quality &&
         *bottom_index >= rc->best_quality);
  assert(q <= rc->worst_quality && q >= rc->best_quality);
  return q;
}

static int get_active_cq_level(const RATE_CONTROL *rc,
                               const VP9EncoderConfig *const oxcf) {
  static const double cq_adjust_threshold = 0.1;
  int active_cq_level = oxcf->cq_level;
  if (oxcf->rc_mode == VPX_CQ &&
      rc->total_target_bits > 0) {
    const double x = (double)rc->total_actual_bits / rc->total_target_bits;
    if (x < cq_adjust_threshold) {
      active_cq_level = (int)(active_cq_level * x / cq_adjust_threshold);
    }
  }
  return active_cq_level;
}

static int rc_pick_q_and_bounds_one_pass_vbr(const VP9_COMP *cpi,
                                             int *bottom_index,
                                             int *top_index) {
  const VP9_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const VP9EncoderConfig *const oxcf = &cpi->oxcf;
  const int cq_level = get_active_cq_level(rc, oxcf);
  int active_best_quality;
  int active_worst_quality = calc_active_worst_quality_one_pass_vbr(cpi);
  int q;
  int *inter_minq;
  ASSIGN_MINQ_TABLE(cm->bit_depth, inter_minq);

  if (frame_is_intra_only(cm)) {

    // Handle the special case for key frames forced when we have reached
    // the maximum key frame interval. Here force the Q to a range
    // based on the ambient Q to reduce the risk of popping.
    if (rc->this_key_frame_forced) {
      int qindex = rc->last_boosted_qindex;
      double last_boosted_q = vp9_convert_qindex_to_q(qindex, cm->bit_depth);
      int delta_qindex = vp9_compute_qdelta(rc, last_boosted_q,
                                            last_boosted_q * 0.75,
                                            cm->bit_depth);
      active_best_quality = MAX(qindex + delta_qindex, rc->best_quality);
    } else {
      // not first frame of one pass and kf_boost is set
      double q_adj_factor = 1.0;
      double q_val;

      active_best_quality =
          get_kf_active_quality(rc, rc->avg_frame_qindex[KEY_FRAME],
                                cm->bit_depth);

      // Allow somewhat lower kf minq with small image formats.
      if ((cm->width * cm->height) <= (352 * 288)) {
        q_adj_factor -= 0.25;
      }

      // Convert the adjustment factor to a qindex delta
      // on active_best_quality.
      q_val = vp9_convert_qindex_to_q(active_best_quality, cm->bit_depth);
      active_best_quality += vp9_compute_qdelta(rc, q_val,
                                                q_val * q_adj_factor,
                                                cm->bit_depth);
    }
  } else if (!rc->is_src_frame_alt_ref &&
             (cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame)) {
    // Use the lower of active_worst_quality and recent
    // average Q as basis for GF/ARF best Q limit unless last frame was
    // a key frame.
    if (rc->frames_since_key > 1 &&
        rc->avg_frame_qindex[INTER_FRAME] < active_worst_quality) {
      q = rc->avg_frame_qindex[INTER_FRAME];
    } else {
      q = rc->avg_frame_qindex[KEY_FRAME];
    }
    // For constrained quality dont allow Q less than the cq level
    if (oxcf->rc_mode == VPX_CQ) {
      if (q < cq_level)
        q = cq_level;

      active_best_quality = get_gf_active_quality(rc, q, cm->bit_depth);

      // Constrained quality use slightly lower active best.
      active_best_quality = active_best_quality * 15 / 16;

    } else if (oxcf->rc_mode == VPX_Q) {
      if (!cpi->refresh_alt_ref_frame) {
        active_best_quality = cq_level;
      } else {
        active_best_quality = get_gf_active_quality(rc, q, cm->bit_depth);
      }
    } else {
      active_best_quality = get_gf_active_quality(rc, q, cm->bit_depth);
    }
  } else {
    if (oxcf->rc_mode == VPX_Q) {
      active_best_quality = cq_level;
    } else {
      // Use the lower of active_worst_quality and recent/average Q.
      if (cm->current_video_frame > 1)
        active_best_quality = inter_minq[rc->avg_frame_qindex[INTER_FRAME]];
      else
        active_best_quality = inter_minq[rc->avg_frame_qindex[KEY_FRAME]];
      // For the constrained quality mode we don't want
      // q to fall below the cq level.
      if ((oxcf->rc_mode == VPX_CQ) &&
          (active_best_quality < cq_level)) {
        active_best_quality = cq_level;
      }
    }
  }

  // Clip the active best and worst quality values to limits
  active_best_quality = clamp(active_best_quality,
                              rc->best_quality, rc->worst_quality);
  active_worst_quality = clamp(active_worst_quality,
                               active_best_quality, rc->worst_quality);

  *top_index = active_worst_quality;
  *bottom_index = active_best_quality;

#if LIMIT_QRANGE_FOR_ALTREF_AND_KEY
  {
    int qdelta = 0;
    vp9_clear_system_state();

    // Limit Q range for the adaptive loop.
    if (cm->frame_type == KEY_FRAME &&
        !rc->this_key_frame_forced &&
        !(cm->current_video_frame == 0)) {
      qdelta = vp9_compute_qdelta_by_rate(&cpi->rc, cm->frame_type,
                                          active_worst_quality, 2.0,
                                          cm->bit_depth);
    } else if (!rc->is_src_frame_alt_ref &&
               (cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame)) {
      qdelta = vp9_compute_qdelta_by_rate(&cpi->rc, cm->frame_type,
                                          active_worst_quality, 1.75,
                                          cm->bit_depth);
    }
    *top_index = active_worst_quality + qdelta;
    *top_index = (*top_index > *bottom_index) ? *top_index : *bottom_index;
  }
#endif

  if (oxcf->rc_mode == VPX_Q) {
    q = active_best_quality;
  // Special case code to try and match quality with forced key frames
  } else if ((cm->frame_type == KEY_FRAME) && rc->this_key_frame_forced) {
    q = rc->last_boosted_qindex;
  } else {
    q = vp9_rc_regulate_q(cpi, rc->this_frame_target,
                          active_best_quality, active_worst_quality);
    if (q > *top_index) {
      // Special case when we are targeting the max allowed rate
      if (rc->this_frame_target >= rc->max_frame_bandwidth)
        *top_index = q;
      else
        q = *top_index;
    }
  }

  assert(*top_index <= rc->worst_quality &&
         *top_index >= rc->best_quality);
  assert(*bottom_index <= rc->worst_quality &&
         *bottom_index >= rc->best_quality);
  assert(q <= rc->worst_quality && q >= rc->best_quality);
  return q;
}

int vp9_frame_type_qdelta(const VP9_COMP *cpi, int rf_level, int q) {
  static const double rate_factor_deltas[RATE_FACTOR_LEVELS] = {
    1.00,  // INTER_NORMAL
    1.00,  // INTER_HIGH
    1.50,  // GF_ARF_LOW
    1.75,  // GF_ARF_STD
    2.00,  // KF_STD
  };
  static const FRAME_TYPE frame_type[RATE_FACTOR_LEVELS] =
      {INTER_FRAME, INTER_FRAME, INTER_FRAME, INTER_FRAME, KEY_FRAME};
  const VP9_COMMON *const cm = &cpi->common;
  int qdelta = vp9_compute_qdelta_by_rate(&cpi->rc, frame_type[rf_level],
                                          q, rate_factor_deltas[rf_level],
                                          cm->bit_depth);
  return qdelta;
}

#define STATIC_MOTION_THRESH 95
static int rc_pick_q_and_bounds_two_pass(const VP9_COMP *cpi,
                                         int *bottom_index,
                                         int *top_index) {
  const VP9_COMMON *const cm = &cpi->common;
  const RATE_CONTROL *const rc = &cpi->rc;
  const VP9EncoderConfig *const oxcf = &cpi->oxcf;
  const GF_GROUP *gf_group = &cpi->twopass.gf_group;
  const int cq_level = get_active_cq_level(rc, oxcf);
  int active_best_quality;
  int active_worst_quality = cpi->twopass.active_worst_quality;
  int q;
  int *inter_minq;
  ASSIGN_MINQ_TABLE(cm->bit_depth, inter_minq);

  if (frame_is_intra_only(cm) || vp9_is_upper_layer_key_frame(cpi)) {
    // Handle the special case for key frames forced when we have reached
    // the maximum key frame interval. Here force the Q to a range
    // based on the ambient Q to reduce the risk of popping.
    if (rc->this_key_frame_forced) {
      double last_boosted_q;
      int delta_qindex;
      int qindex;

      if (cpi->twopass.last_kfgroup_zeromotion_pct >= STATIC_MOTION_THRESH) {
        qindex = MIN(rc->last_kf_qindex, rc->last_boosted_qindex);
        active_best_quality = qindex;
        last_boosted_q = vp9_convert_qindex_to_q(qindex, cm->bit_depth);
        delta_qindex = vp9_compute_qdelta(rc, last_boosted_q,
                                              last_boosted_q * 1.25,
                                              cm->bit_depth);
        active_worst_quality = MIN(qindex + delta_qindex, active_worst_quality);

      } else {
        qindex = rc->last_boosted_qindex;
        last_boosted_q = vp9_convert_qindex_to_q(qindex, cm->bit_depth);
        delta_qindex = vp9_compute_qdelta(rc, last_boosted_q,
                                              last_boosted_q * 0.75,
                                              cm->bit_depth);
        active_best_quality = MAX(qindex + delta_qindex, rc->best_quality);
      }
    } else {
      // Not forced keyframe.
      double q_adj_factor = 1.0;
      double q_val;
      // Baseline value derived from cpi->active_worst_quality and kf boost.
      active_best_quality = get_kf_active_quality(rc, active_worst_quality,
                                                  cm->bit_depth);

      // Allow somewhat lower kf minq with small image formats.
      if ((cm->width * cm->height) <= (352 * 288)) {
        q_adj_factor -= 0.25;
      }

      // Make a further adjustment based on the kf zero motion measure.
      q_adj_factor += 0.05 - (0.001 * (double)cpi->twopass.kf_zeromotion_pct);

      // Convert the adjustment factor to a qindex delta
      // on active_best_quality.
      q_val = vp9_convert_qindex_to_q(active_best_quality, cm->bit_depth);
      active_best_quality += vp9_compute_qdelta(rc, q_val,
                                                q_val * q_adj_factor,
                                                cm->bit_depth);
    }
  } else if (!rc->is_src_frame_alt_ref &&
             (cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame)) {
    // Use the lower of active_worst_quality and recent
    // average Q as basis for GF/ARF best Q limit unless last frame was
    // a key frame.
    if (rc->frames_since_key > 1 &&
        rc->avg_frame_qindex[INTER_FRAME] < active_worst_quality) {
      q = rc->avg_frame_qindex[INTER_FRAME];
    } else {
      q = active_worst_quality;
    }
    // For constrained quality dont allow Q less than the cq level
    if (oxcf->rc_mode == VPX_CQ) {
      if (q < cq_level)
        q = cq_level;

      active_best_quality = get_gf_active_quality(rc, q, cm->bit_depth);

      // Constrained quality use slightly lower active best.
      active_best_quality = active_best_quality * 15 / 16;

    } else if (oxcf->rc_mode == VPX_Q) {
      if (!cpi->refresh_alt_ref_frame) {
        active_best_quality = cq_level;
      } else {
       active_best_quality = get_gf_active_quality(rc, q, cm->bit_depth);

        // Modify best quality for second level arfs. For mode VPX_Q this
        // becomes the baseline frame q.
        if (gf_group->rf_level[gf_group->index] == GF_ARF_LOW)
          active_best_quality = (active_best_quality + cq_level + 1) / 2;
      }
    } else {
      active_best_quality = get_gf_active_quality(rc, q, cm->bit_depth);
    }
  } else {
    if (oxcf->rc_mode == VPX_Q) {
      active_best_quality = cq_level;
    } else {
      active_best_quality = inter_minq[active_worst_quality];

      // For the constrained quality mode we don't want
      // q to fall below the cq level.
      if ((oxcf->rc_mode == VPX_CQ) &&
          (active_best_quality < cq_level)) {
        active_best_quality = cq_level;
      }
    }
  }

  // Extension to max or min Q if undershoot or overshoot is outside
  // the permitted range.
  if ((cpi->oxcf.rc_mode != VPX_Q) &&
      (cpi->twopass.gf_zeromotion_pct < VLOW_MOTION_THRESHOLD)) {
    if (frame_is_intra_only(cm) ||
        (!rc->is_src_frame_alt_ref &&
         (cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame))) {
      active_best_quality -=
        (cpi->twopass.extend_minq + cpi->twopass.extend_minq_fast);
      active_worst_quality += (cpi->twopass.extend_maxq / 2);
    } else {
      active_best_quality -=
        (cpi->twopass.extend_minq + cpi->twopass.extend_minq_fast) / 2;
      active_worst_quality += cpi->twopass.extend_maxq;
    }
  }

#if LIMIT_QRANGE_FOR_ALTREF_AND_KEY
  vp9_clear_system_state();
  // Static forced key frames Q restrictions dealt with elsewhere.
  if (!((frame_is_intra_only(cm) || vp9_is_upper_layer_key_frame(cpi))) ||
      !rc->this_key_frame_forced ||
      (cpi->twopass.last_kfgroup_zeromotion_pct < STATIC_MOTION_THRESH)) {
    int qdelta = vp9_frame_type_qdelta(cpi, gf_group->rf_level[gf_group->index],
                                       active_worst_quality);
    active_worst_quality = MAX(active_worst_quality + qdelta,
                               active_best_quality);
  }
#endif

  // Modify active_best_quality for downscaled normal frames.
  if (rc->frame_size_selector != UNSCALED && !frame_is_kf_gf_arf(cpi)) {
    int qdelta = vp9_compute_qdelta_by_rate(rc, cm->frame_type,
                                            active_best_quality, 2.0,
                                            cm->bit_depth);
    active_best_quality = MAX(active_best_quality + qdelta, rc->best_quality);
  }

  active_best_quality = clamp(active_best_quality,
                              rc->best_quality, rc->worst_quality);
  active_worst_quality = clamp(active_worst_quality,
                               active_best_quality, rc->worst_quality);

  if (oxcf->rc_mode == VPX_Q) {
    q = active_best_quality;
  // Special case code to try and match quality with forced key frames.
  } else if ((frame_is_intra_only(cm) || vp9_is_upper_layer_key_frame(cpi)) &&
             rc->this_key_frame_forced) {
    // If static since last kf use better of last boosted and last kf q.
    if (cpi->twopass.last_kfgroup_zeromotion_pct >= STATIC_MOTION_THRESH) {
      q = MIN(rc->last_kf_qindex, rc->last_boosted_qindex);
    } else {
      q = rc->last_boosted_qindex;
    }
  } else {
    q = vp9_rc_regulate_q(cpi, rc->this_frame_target,
                          active_best_quality, active_worst_quality);
    if (q > active_worst_quality) {
      // Special case when we are targeting the max allowed rate.
      if (rc->this_frame_target >= rc->max_frame_bandwidth)
        active_worst_quality = q;
      else
        q = active_worst_quality;
    }
  }
  clamp(q, active_best_quality, active_worst_quality);

  *top_index = active_worst_quality;
  *bottom_index = active_best_quality;

  assert(*top_index <= rc->worst_quality &&
         *top_index >= rc->best_quality);
  assert(*bottom_index <= rc->worst_quality &&
         *bottom_index >= rc->best_quality);
  assert(q <= rc->worst_quality && q >= rc->best_quality);
  return q;
}

int vp9_rc_pick_q_and_bounds(const VP9_COMP *cpi,
                             int *bottom_index, int *top_index) {
  int q;
  if (cpi->oxcf.pass == 0) {
    if (cpi->oxcf.rc_mode == VPX_CBR)
      q = rc_pick_q_and_bounds_one_pass_cbr(cpi, bottom_index, top_index);
    else
      q = rc_pick_q_and_bounds_one_pass_vbr(cpi, bottom_index, top_index);
  } else {
    q = rc_pick_q_and_bounds_two_pass(cpi, bottom_index, top_index);
  }
  if (cpi->sf.use_nonrd_pick_mode) {
    if (cpi->sf.force_frame_boost == 1)
      q -= cpi->sf.max_delta_qindex;

    if (q < *bottom_index)
      *bottom_index = q;
    else if (q > *top_index)
      *top_index = q;
  }
  return q;
}

void vp9_rc_compute_frame_size_bounds(const VP9_COMP *cpi,
                                      int frame_target,
                                      int *frame_under_shoot_limit,
                                      int *frame_over_shoot_limit) {
  if (cpi->oxcf.rc_mode == VPX_Q) {
    *frame_under_shoot_limit = 0;
    *frame_over_shoot_limit  = INT_MAX;
  } else {
    // For very small rate targets where the fractional adjustment
    // may be tiny make sure there is at least a minimum range.
    const int tolerance = (cpi->sf.recode_tolerance * frame_target) / 100;
    *frame_under_shoot_limit = MAX(frame_target - tolerance - 200, 0);
    *frame_over_shoot_limit = MIN(frame_target + tolerance + 200,
                                  cpi->rc.max_frame_bandwidth);
  }
}

void vp9_rc_set_frame_target(VP9_COMP *cpi, int target) {
  const VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;

  rc->this_frame_target = target;

  // Modify frame size target when down-scaling.
  if (cpi->oxcf.resize_mode == RESIZE_DYNAMIC &&
      rc->frame_size_selector != UNSCALED)
    rc->this_frame_target = (int)(rc->this_frame_target
        * rate_thresh_mult[rc->frame_size_selector]);

  // Target rate per SB64 (including partial SB64s.
  rc->sb64_target_rate = ((int64_t)rc->this_frame_target * 64 * 64) /
                             (cm->width * cm->height);
}

static void update_alt_ref_frame_stats(VP9_COMP *cpi) {
  // this frame refreshes means next frames don't unless specified by user
  RATE_CONTROL *const rc = &cpi->rc;
  rc->frames_since_golden = 0;

  // Mark the alt ref as done (setting to 0 means no further alt refs pending).
  rc->source_alt_ref_pending = 0;

  // Set the alternate reference frame active flag
  rc->source_alt_ref_active = 1;
}

static void update_golden_frame_stats(VP9_COMP *cpi) {
  RATE_CONTROL *const rc = &cpi->rc;

  // Update the Golden frame usage counts.
  if (cpi->refresh_golden_frame) {
    // this frame refreshes means next frames don't unless specified by user
    rc->frames_since_golden = 0;

    // If we are not using alt ref in the up and coming group clear the arf
    // active flag.
    if (!rc->source_alt_ref_pending) {
      rc->source_alt_ref_active = 0;
    }

    // Decrement count down till next gf
    if (rc->frames_till_gf_update_due > 0)
      rc->frames_till_gf_update_due--;

  } else if (!cpi->refresh_alt_ref_frame) {
    // Decrement count down till next gf
    if (rc->frames_till_gf_update_due > 0)
      rc->frames_till_gf_update_due--;

    rc->frames_since_golden++;
  }
}

void vp9_rc_postencode_update(VP9_COMP *cpi, uint64_t bytes_used) {
  const VP9_COMMON *const cm = &cpi->common;
  const VP9EncoderConfig *const oxcf = &cpi->oxcf;
  RATE_CONTROL *const rc = &cpi->rc;
  const int qindex = cm->base_qindex;

  if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ && cm->seg.enabled) {
    vp9_cyclic_refresh_postencode(cpi);
  }

  // Update rate control heuristics
  rc->projected_frame_size = (int)(bytes_used << 3);

  // Post encode loop adjustment of Q prediction.
  vp9_rc_update_rate_correction_factors(cpi);

  // Keep a record of last Q and ambient average Q.
  if (cm->frame_type == KEY_FRAME) {
    rc->last_q[KEY_FRAME] = qindex;
    rc->avg_frame_qindex[KEY_FRAME] =
        ROUND_POWER_OF_TWO(3 * rc->avg_frame_qindex[KEY_FRAME] + qindex, 2);
  } else {
    if (rc->is_src_frame_alt_ref ||
        !(cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame) ||
        (cpi->use_svc && oxcf->rc_mode == VPX_CBR)) {
      rc->last_q[INTER_FRAME] = qindex;
      rc->avg_frame_qindex[INTER_FRAME] =
        ROUND_POWER_OF_TWO(3 * rc->avg_frame_qindex[INTER_FRAME] + qindex, 2);
      rc->ni_frames++;
      rc->tot_q += vp9_convert_qindex_to_q(qindex, cm->bit_depth);
      rc->avg_q = rc->tot_q / rc->ni_frames;
      // Calculate the average Q for normal inter frames (not key or GFU
      // frames).
      rc->ni_tot_qi += qindex;
      rc->ni_av_qi = rc->ni_tot_qi / rc->ni_frames;
    }
  }

  // Keep record of last boosted (KF/KF/ARF) Q value.
  // If the current frame is coded at a lower Q then we also update it.
  // If all mbs in this group are skipped only update if the Q value is
  // better than that already stored.
  // This is used to help set quality in forced key frames to reduce popping
  if ((qindex < rc->last_boosted_qindex) ||
      (cm->frame_type == KEY_FRAME) ||
      (!rc->constrained_gf_group &&
       (cpi->refresh_alt_ref_frame ||
        (cpi->refresh_golden_frame && !rc->is_src_frame_alt_ref)))) {
    rc->last_boosted_qindex = qindex;
  }
  if (cm->frame_type == KEY_FRAME)
    rc->last_kf_qindex = qindex;

  update_buffer_level(cpi, rc->projected_frame_size);

  // Rolling monitors of whether we are over or underspending used to help
  // regulate min and Max Q in two pass.
  if (cm->frame_type != KEY_FRAME) {
    rc->rolling_target_bits = ROUND_POWER_OF_TWO(
        rc->rolling_target_bits * 3 + rc->this_frame_target, 2);
    rc->rolling_actual_bits = ROUND_POWER_OF_TWO(
        rc->rolling_actual_bits * 3 + rc->projected_frame_size, 2);
    rc->long_rolling_target_bits = ROUND_POWER_OF_TWO(
        rc->long_rolling_target_bits * 31 + rc->this_frame_target, 5);
    rc->long_rolling_actual_bits = ROUND_POWER_OF_TWO(
        rc->long_rolling_actual_bits * 31 + rc->projected_frame_size, 5);
  }

  // Actual bits spent
  rc->total_actual_bits += rc->projected_frame_size;
  rc->total_target_bits += cm->show_frame ? rc->avg_frame_bandwidth : 0;

  rc->total_target_vs_actual = rc->total_actual_bits - rc->total_target_bits;

  if (is_altref_enabled(cpi) && cpi->refresh_alt_ref_frame &&
      (cm->frame_type != KEY_FRAME))
    // Update the alternate reference frame stats as appropriate.
    update_alt_ref_frame_stats(cpi);
  else
    // Update the Golden frame stats as appropriate.
    update_golden_frame_stats(cpi);

  if (cm->frame_type == KEY_FRAME)
    rc->frames_since_key = 0;
  if (cm->show_frame) {
    rc->frames_since_key++;
    rc->frames_to_key--;
  }

  // Trigger the resizing of the next frame if it is scaled.
  if (oxcf->pass != 0) {
    cpi->resize_pending =
        rc->next_frame_size_selector != rc->frame_size_selector;
    rc->frame_size_selector = rc->next_frame_size_selector;
  }
}

void vp9_rc_postencode_update_drop_frame(VP9_COMP *cpi) {
  // Update buffer level with zero size, update frame counters, and return.
  update_buffer_level(cpi, 0);
  cpi->rc.frames_since_key++;
  cpi->rc.frames_to_key--;
  cpi->rc.rc_2_frame = 0;
  cpi->rc.rc_1_frame = 0;
}

// Use this macro to turn on/off use of alt-refs in one-pass mode.
#define USE_ALTREF_FOR_ONE_PASS   1

static int calc_pframe_target_size_one_pass_vbr(const VP9_COMP *const cpi) {
  static const int af_ratio = 10;
  const RATE_CONTROL *const rc = &cpi->rc;
  int target;
#if USE_ALTREF_FOR_ONE_PASS
  target = (!rc->is_src_frame_alt_ref &&
            (cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame)) ?
      (rc->avg_frame_bandwidth * rc->baseline_gf_interval * af_ratio) /
      (rc->baseline_gf_interval + af_ratio - 1) :
      (rc->avg_frame_bandwidth * rc->baseline_gf_interval) /
      (rc->baseline_gf_interval + af_ratio - 1);
#else
  target = rc->avg_frame_bandwidth;
#endif
  return vp9_rc_clamp_pframe_target_size(cpi, target);
}

static int calc_iframe_target_size_one_pass_vbr(const VP9_COMP *const cpi) {
  static const int kf_ratio = 25;
  const RATE_CONTROL *rc = &cpi->rc;
  const int target = rc->avg_frame_bandwidth * kf_ratio;
  return vp9_rc_clamp_iframe_target_size(cpi, target);
}

void vp9_rc_get_one_pass_vbr_params(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  int target;
  // TODO(yaowu): replace the "auto_key && 0" below with proper decision logic.
  if (!cpi->refresh_alt_ref_frame &&
      (cm->current_video_frame == 0 ||
       (cpi->frame_flags & FRAMEFLAGS_KEY) ||
       rc->frames_to_key == 0 ||
       (cpi->oxcf.auto_key && 0))) {
    cm->frame_type = KEY_FRAME;
    rc->this_key_frame_forced = cm->current_video_frame != 0 &&
                                rc->frames_to_key == 0;
    rc->frames_to_key = cpi->oxcf.key_freq;
    rc->kf_boost = DEFAULT_KF_BOOST;
    rc->source_alt_ref_active = 0;
  } else {
    cm->frame_type = INTER_FRAME;
  }
  if (rc->frames_till_gf_update_due == 0) {
    rc->baseline_gf_interval = (rc->min_gf_interval + rc->max_gf_interval) / 2;
    rc->frames_till_gf_update_due = rc->baseline_gf_interval;
    // NOTE: frames_till_gf_update_due must be <= frames_to_key.
    if (rc->frames_till_gf_update_due > rc->frames_to_key) {
      rc->frames_till_gf_update_due = rc->frames_to_key;
      rc->constrained_gf_group = 1;
    } else {
      rc->constrained_gf_group = 0;
    }
    cpi->refresh_golden_frame = 1;
    rc->source_alt_ref_pending = USE_ALTREF_FOR_ONE_PASS;
    rc->gfu_boost = DEFAULT_GF_BOOST;
  }
  if (cm->frame_type == KEY_FRAME)
    target = calc_iframe_target_size_one_pass_vbr(cpi);
  else
    target = calc_pframe_target_size_one_pass_vbr(cpi);
  vp9_rc_set_frame_target(cpi, target);
}

static int calc_pframe_target_size_one_pass_cbr(const VP9_COMP *cpi) {
  const VP9EncoderConfig *oxcf = &cpi->oxcf;
  const RATE_CONTROL *rc = &cpi->rc;
  const SVC *const svc = &cpi->svc;
  const int64_t diff = rc->optimal_buffer_level - rc->buffer_level;
  const int64_t one_pct_bits = 1 + rc->optimal_buffer_level / 100;
  int min_frame_target = MAX(rc->avg_frame_bandwidth >> 4, FRAME_OVERHEAD_BITS);
  int target;

  if (oxcf->gf_cbr_boost_pct) {
    const int af_ratio_pct = oxcf->gf_cbr_boost_pct + 100;
    target =  cpi->refresh_golden_frame ?
      (rc->avg_frame_bandwidth * rc->baseline_gf_interval * af_ratio_pct) /
      (rc->baseline_gf_interval * 100 + af_ratio_pct - 100) :
      (rc->avg_frame_bandwidth * rc->baseline_gf_interval * 100) /
      (rc->baseline_gf_interval * 100 + af_ratio_pct - 100);
  } else {
    target = rc->avg_frame_bandwidth;
  }
  if (is_one_pass_cbr_svc(cpi)) {
    // Note that for layers, avg_frame_bandwidth is the cumulative
    // per-frame-bandwidth. For the target size of this frame, use the
    // layer average frame size (i.e., non-cumulative per-frame-bw).
    int layer =
        LAYER_IDS_TO_IDX(svc->spatial_layer_id,
            svc->temporal_layer_id, svc->number_temporal_layers);
    const LAYER_CONTEXT *lc = &svc->layer_context[layer];
    target = lc->avg_frame_size;
    min_frame_target = MAX(lc->avg_frame_size >> 4, FRAME_OVERHEAD_BITS);
  }
  if (diff > 0) {
    // Lower the target bandwidth for this frame.
    const int pct_low = (int)MIN(diff / one_pct_bits, oxcf->under_shoot_pct);
    target -= (target * pct_low) / 200;
  } else if (diff < 0) {
    // Increase the target bandwidth for this frame.
    const int pct_high = (int)MIN(-diff / one_pct_bits, oxcf->over_shoot_pct);
    target += (target * pct_high) / 200;
  }
  if (oxcf->rc_max_inter_bitrate_pct) {
    const int max_rate = rc->avg_frame_bandwidth *
                         oxcf->rc_max_inter_bitrate_pct / 100;
    target = MIN(target, max_rate);
  }
  return MAX(min_frame_target, target);
}

static int calc_iframe_target_size_one_pass_cbr(const VP9_COMP *cpi) {
  const RATE_CONTROL *rc = &cpi->rc;
  const VP9EncoderConfig *oxcf = &cpi->oxcf;
  const SVC *const svc = &cpi->svc;
  int target;
  if (cpi->common.current_video_frame == 0) {
    target = ((rc->starting_buffer_level / 2) > INT_MAX)
      ? INT_MAX : (int)(rc->starting_buffer_level / 2);
  } else {
    int kf_boost = 32;
    double framerate = cpi->framerate;
    if (svc->number_temporal_layers > 1 &&
        oxcf->rc_mode == VPX_CBR) {
      // Use the layer framerate for temporal layers CBR mode.
      const int layer = LAYER_IDS_TO_IDX(svc->spatial_layer_id,
          svc->temporal_layer_id, svc->number_temporal_layers);
      const LAYER_CONTEXT *lc = &svc->layer_context[layer];
      framerate = lc->framerate;
    }
    kf_boost = MAX(kf_boost, (int)(2 * framerate - 16));
    if (rc->frames_since_key <  framerate / 2) {
      kf_boost = (int)(kf_boost * rc->frames_since_key /
                       (framerate / 2));
    }
    target = ((16 + kf_boost) * rc->avg_frame_bandwidth) >> 4;
  }
  return vp9_rc_clamp_iframe_target_size(cpi, target);
}

// Reset information needed to set proper reference frames and buffer updates
// for temporal layering. This is called when a key frame is encoded.
static void reset_temporal_layer_to_zero(VP9_COMP *cpi) {
  int sl;
  LAYER_CONTEXT *lc = NULL;
  cpi->svc.temporal_layer_id = 0;

  for (sl = 0; sl < cpi->svc.number_spatial_layers; ++sl) {
    lc = &cpi->svc.layer_context[sl * cpi->svc.number_temporal_layers];
    lc->current_video_frame_in_layer = 0;
    lc->frames_from_key_frame = 0;
  }
}

void vp9_rc_get_svc_params(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  int target = rc->avg_frame_bandwidth;
  const int layer = LAYER_IDS_TO_IDX(cpi->svc.spatial_layer_id,
      cpi->svc.temporal_layer_id, cpi->svc.number_temporal_layers);

  if ((cm->current_video_frame == 0) ||
      (cpi->frame_flags & FRAMEFLAGS_KEY) ||
      (cpi->oxcf.auto_key && (rc->frames_since_key %
          cpi->oxcf.key_freq == 0))) {
    cm->frame_type = KEY_FRAME;
    rc->source_alt_ref_active = 0;

    if (is_two_pass_svc(cpi)) {
      cpi->svc.layer_context[layer].is_key_frame = 1;
      cpi->ref_frame_flags &=
          (~VP9_LAST_FLAG & ~VP9_GOLD_FLAG & ~VP9_ALT_FLAG);
    } else if (is_one_pass_cbr_svc(cpi)) {
      cpi->svc.layer_context[layer].is_key_frame = 1;
      reset_temporal_layer_to_zero(cpi);
      cpi->ref_frame_flags &=
                (~VP9_LAST_FLAG & ~VP9_GOLD_FLAG & ~VP9_ALT_FLAG);
      // Assumption here is that LAST_FRAME is being updated for a keyframe.
      // Thus no change in update flags.
      target = calc_iframe_target_size_one_pass_cbr(cpi);
    }
  } else {
    cm->frame_type = INTER_FRAME;
    if (is_two_pass_svc(cpi)) {
      LAYER_CONTEXT *lc = &cpi->svc.layer_context[layer];
      if (cpi->svc.spatial_layer_id == 0) {
        lc->is_key_frame = 0;
      } else {
        lc->is_key_frame =
            cpi->svc.layer_context[cpi->svc.temporal_layer_id].is_key_frame;
        if (lc->is_key_frame)
          cpi->ref_frame_flags &= (~VP9_LAST_FLAG);
      }
      cpi->ref_frame_flags &= (~VP9_ALT_FLAG);
    } else if (is_one_pass_cbr_svc(cpi)) {
      LAYER_CONTEXT *lc = &cpi->svc.layer_context[layer];
      if (cpi->svc.spatial_layer_id == 0) {
        lc->is_key_frame = 0;
      } else {
        lc->is_key_frame =
            cpi->svc.layer_context[cpi->svc.temporal_layer_id].is_key_frame;
      }
      target = calc_pframe_target_size_one_pass_cbr(cpi);
    }
  }

  // Any update/change of global cyclic refresh parameters (amount/delta-qp)
  // should be done here, before the frame qp is selected.
  if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ)
    vp9_cyclic_refresh_update_parameters(cpi);

  vp9_rc_set_frame_target(cpi, target);
  rc->frames_till_gf_update_due = INT_MAX;
  rc->baseline_gf_interval = INT_MAX;
}

void vp9_rc_get_one_pass_cbr_params(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  int target;
  // TODO(yaowu): replace the "auto_key && 0" below with proper decision logic.
  if ((cm->current_video_frame == 0 ||
      (cpi->frame_flags & FRAMEFLAGS_KEY) ||
      rc->frames_to_key == 0 ||
      (cpi->oxcf.auto_key && 0))) {
    cm->frame_type = KEY_FRAME;
    rc->this_key_frame_forced = cm->current_video_frame != 0 &&
                                rc->frames_to_key == 0;
    rc->frames_to_key = cpi->oxcf.key_freq;
    rc->kf_boost = DEFAULT_KF_BOOST;
    rc->source_alt_ref_active = 0;
  } else {
    cm->frame_type = INTER_FRAME;
  }
  if (rc->frames_till_gf_update_due == 0) {
    if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ)
      vp9_cyclic_refresh_set_golden_update(cpi);
    else
      rc->baseline_gf_interval =
          (rc->min_gf_interval + rc->max_gf_interval) / 2;
    rc->frames_till_gf_update_due = rc->baseline_gf_interval;
    // NOTE: frames_till_gf_update_due must be <= frames_to_key.
    if (rc->frames_till_gf_update_due > rc->frames_to_key)
      rc->frames_till_gf_update_due = rc->frames_to_key;
    cpi->refresh_golden_frame = 1;
    rc->gfu_boost = DEFAULT_GF_BOOST;
  }

  // Any update/change of global cyclic refresh parameters (amount/delta-qp)
  // should be done here, before the frame qp is selected.
  if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ)
    vp9_cyclic_refresh_update_parameters(cpi);

  if (cm->frame_type == KEY_FRAME)
    target = calc_iframe_target_size_one_pass_cbr(cpi);
  else
    target = calc_pframe_target_size_one_pass_cbr(cpi);

  vp9_rc_set_frame_target(cpi, target);
  if (cpi->oxcf.resize_mode == RESIZE_DYNAMIC)
    cpi->resize_pending = vp9_resize_one_pass_cbr(cpi);
  else
    cpi->resize_pending = 0;
}

int vp9_compute_qdelta(const RATE_CONTROL *rc, double qstart, double qtarget,
                       vpx_bit_depth_t bit_depth) {
  int start_index = rc->worst_quality;
  int target_index = rc->worst_quality;
  int i;

  // Convert the average q value to an index.
  for (i = rc->best_quality; i < rc->worst_quality; ++i) {
    start_index = i;
    if (vp9_convert_qindex_to_q(i, bit_depth) >= qstart)
      break;
  }

  // Convert the q target to an index
  for (i = rc->best_quality; i < rc->worst_quality; ++i) {
    target_index = i;
    if (vp9_convert_qindex_to_q(i, bit_depth) >= qtarget)
      break;
  }

  return target_index - start_index;
}

int vp9_compute_qdelta_by_rate(const RATE_CONTROL *rc, FRAME_TYPE frame_type,
                               int qindex, double rate_target_ratio,
                               vpx_bit_depth_t bit_depth) {
  int target_index = rc->worst_quality;
  int i;

  // Look up the current projected bits per block for the base index
  const int base_bits_per_mb = vp9_rc_bits_per_mb(frame_type, qindex, 1.0,
                                                  bit_depth);

  // Find the target bits per mb based on the base value and given ratio.
  const int target_bits_per_mb = (int)(rate_target_ratio * base_bits_per_mb);

  // Convert the q target to an index
  for (i = rc->best_quality; i < rc->worst_quality; ++i) {
    if (vp9_rc_bits_per_mb(frame_type, i, 1.0, bit_depth) <=
        target_bits_per_mb) {
      target_index = i;
      break;
    }
  }
  return target_index - qindex;
}

void vp9_rc_set_gf_interval_range(const VP9_COMP *const cpi,
                                  RATE_CONTROL *const rc) {
  const VP9EncoderConfig *const oxcf = &cpi->oxcf;

  // Set Maximum gf/arf interval
  rc->max_gf_interval = oxcf->max_gf_interval;
  rc->min_gf_interval = oxcf->min_gf_interval;
  if (rc->min_gf_interval == 0)
    rc->min_gf_interval = vp9_rc_get_default_min_gf_interval(
        oxcf->width, oxcf->height, cpi->framerate);
  if (rc->max_gf_interval == 0)
    rc->max_gf_interval = vp9_rc_get_default_max_gf_interval(
        cpi->framerate, rc->min_gf_interval);

  // Extended interval for genuinely static scenes
  rc->static_scene_max_gf_interval = MAX_LAG_BUFFERS * 2;

  if (is_altref_enabled(cpi)) {
    if (rc->static_scene_max_gf_interval > oxcf->lag_in_frames - 1)
      rc->static_scene_max_gf_interval = oxcf->lag_in_frames - 1;
  }

  if (rc->max_gf_interval > rc->static_scene_max_gf_interval)
    rc->max_gf_interval = rc->static_scene_max_gf_interval;

  // Clamp min to max
  rc->min_gf_interval = MIN(rc->min_gf_interval, rc->max_gf_interval);
}

void vp9_rc_update_framerate(VP9_COMP *cpi) {
  const VP9_COMMON *const cm = &cpi->common;
  const VP9EncoderConfig *const oxcf = &cpi->oxcf;
  RATE_CONTROL *const rc = &cpi->rc;
  int vbr_max_bits;

  rc->avg_frame_bandwidth = (int)(oxcf->target_bandwidth / cpi->framerate);
  rc->min_frame_bandwidth = (int)(rc->avg_frame_bandwidth *
                                oxcf->two_pass_vbrmin_section / 100);

  rc->min_frame_bandwidth = MAX(rc->min_frame_bandwidth, FRAME_OVERHEAD_BITS);

  // A maximum bitrate for a frame is defined.
  // The baseline for this aligns with HW implementations that
  // can support decode of 1080P content up to a bitrate of MAX_MB_RATE bits
  // per 16x16 MB (averaged over a frame). However this limit is extended if
  // a very high rate is given on the command line or the the rate cannnot
  // be acheived because of a user specificed max q (e.g. when the user
  // specifies lossless encode.
  vbr_max_bits = (int)(((int64_t)rc->avg_frame_bandwidth *
                     oxcf->two_pass_vbrmax_section) / 100);
  rc->max_frame_bandwidth = MAX(MAX((cm->MBs * MAX_MB_RATE), MAXRATE_1080P),
                                    vbr_max_bits);

  vp9_rc_set_gf_interval_range(cpi, rc);
}

#define VBR_PCT_ADJUSTMENT_LIMIT 50
// For VBR...adjustment to the frame target based on error from previous frames
static void vbr_rate_correction(VP9_COMP *cpi, int *this_frame_target) {
  RATE_CONTROL *const rc = &cpi->rc;
  int64_t vbr_bits_off_target = rc->vbr_bits_off_target;
  int max_delta;
  double position_factor = 1.0;

  // How far through the clip are we.
  // This number is used to damp the per frame rate correction.
  // Range 0 - 1.0
  if (cpi->twopass.total_stats.count) {
    position_factor = sqrt((double)cpi->common.current_video_frame /
                           cpi->twopass.total_stats.count);
  }
  max_delta = (int)(position_factor *
                    ((*this_frame_target * VBR_PCT_ADJUSTMENT_LIMIT) / 100));

  // vbr_bits_off_target > 0 means we have extra bits to spend
  if (vbr_bits_off_target > 0) {
    *this_frame_target +=
      (vbr_bits_off_target > max_delta) ? max_delta
                                        : (int)vbr_bits_off_target;
  } else {
    *this_frame_target -=
      (vbr_bits_off_target < -max_delta) ? max_delta
                                         : (int)-vbr_bits_off_target;
  }

  // Fast redistribution of bits arising from massive local undershoot.
  // Dont do it for kf,arf,gf or overlay frames.
  if (!frame_is_kf_gf_arf(cpi) && !rc->is_src_frame_alt_ref &&
      rc->vbr_bits_off_target_fast) {
    int one_frame_bits = MAX(rc->avg_frame_bandwidth, *this_frame_target);
    int fast_extra_bits;
    fast_extra_bits =
      (int)MIN(rc->vbr_bits_off_target_fast, one_frame_bits);
    fast_extra_bits = (int)MIN(fast_extra_bits,
      MAX(one_frame_bits / 8, rc->vbr_bits_off_target_fast / 8));
    *this_frame_target += (int)fast_extra_bits;
    rc->vbr_bits_off_target_fast -= fast_extra_bits;
  }
}

void vp9_set_target_rate(VP9_COMP *cpi) {
  RATE_CONTROL *const rc = &cpi->rc;
  int target_rate = rc->base_frame_target;

  // Correction to rate target based on prior over or under shoot.
  if (cpi->oxcf.rc_mode == VPX_VBR || cpi->oxcf.rc_mode == VPX_CQ)
    vbr_rate_correction(cpi, &target_rate);
  vp9_rc_set_frame_target(cpi, target_rate);
}

// Check if we should resize, based on average QP from past x frames.
// Only allow for resize at most one scale down for now, scaling factor is 2.
int vp9_resize_one_pass_cbr(VP9_COMP *cpi) {
  const VP9_COMMON *const cm = &cpi->common;
  RATE_CONTROL *const rc = &cpi->rc;
  int resize_now = 0;
  cpi->resize_scale_num = 1;
  cpi->resize_scale_den = 1;
  // Don't resize on key frame; reset the counters on key frame.
  if (cm->frame_type == KEY_FRAME) {
    cpi->resize_avg_qp = 0;
    cpi->resize_count = 0;
    return 0;
  }
  // Resize based on average buffer underflow and QP over some window.
  // Ignore samples close to key frame, since QP is usually high after key.
  if (cpi->rc.frames_since_key > 2 * cpi->framerate) {
    const int window = (int)(5 * cpi->framerate);
    cpi->resize_avg_qp += cm->base_qindex;
    if (cpi->rc.buffer_level < (int)(30 * rc->optimal_buffer_level / 100))
      ++cpi->resize_buffer_underflow;
    ++cpi->resize_count;
    // Check for resize action every "window" frames.
    if (cpi->resize_count >= window) {
      int avg_qp = cpi->resize_avg_qp / cpi->resize_count;
      // Resize down if buffer level has underflowed sufficent amount in past
      // window, and we are at original resolution.
      // Resize back up if average QP is low, and we are currently in a resized
      // down state.
      if (cpi->resize_state == 0 &&
          cpi->resize_buffer_underflow > (cpi->resize_count >> 2)) {
        resize_now = 1;
        cpi->resize_state = 1;
      } else if (cpi->resize_state == 1 &&
                 avg_qp < 40 * cpi->rc.worst_quality / 100) {
        resize_now = -1;
        cpi->resize_state = 0;
      }
      // Reset for next window measurement.
      cpi->resize_avg_qp = 0;
      cpi->resize_count = 0;
      cpi->resize_buffer_underflow = 0;
    }
  }
  // If decision is to resize, reset some quantities, and check is we should
  // reduce rate correction factor,
  if (resize_now != 0) {
    int target_bits_per_frame;
    int active_worst_quality;
    int qindex;
    int tot_scale_change;
    // For now, resize is by 1/2 x 1/2.
    cpi->resize_scale_num = 1;
    cpi->resize_scale_den = 2;
    tot_scale_change = (cpi->resize_scale_den * cpi->resize_scale_den) /
        (cpi->resize_scale_num * cpi->resize_scale_num);
    // Reset buffer level to optimal, update target size.
    rc->buffer_level = rc->optimal_buffer_level;
    rc->bits_off_target = rc->optimal_buffer_level;
    rc->this_frame_target = calc_pframe_target_size_one_pass_cbr(cpi);
    // Reset cyclic refresh parameters.
    if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ && cm->seg.enabled)
      vp9_cyclic_refresh_reset_resize(cpi);
    // Get the projected qindex, based on the scaled target frame size (scaled
    // so target_bits_per_mb in vp9_rc_regulate_q will be correct target).
    target_bits_per_frame = (resize_now == 1) ?
        rc->this_frame_target * tot_scale_change :
        rc->this_frame_target / tot_scale_change;
    active_worst_quality = calc_active_worst_quality_one_pass_cbr(cpi);
    qindex = vp9_rc_regulate_q(cpi,
                               target_bits_per_frame,
                               rc->best_quality,
                               active_worst_quality);
    // If resize is down, check if projected q index is close to worst_quality,
    // and if so, reduce the rate correction factor (since likely can afford
    // lower q for resized frame).
    if (resize_now == 1 &&
        qindex > 90 * cpi->rc.worst_quality / 100) {
      rc->rate_correction_factors[INTER_NORMAL] *= 0.85;
    }
    // If resize is back up, check if projected q index is too much above the
    // current base_qindex, and if so, reduce the rate correction factor
    // (since prefer to keep q for resized frame at least close to previous q).
    if (resize_now == -1 &&
       qindex > 130 * cm->base_qindex / 100) {
      rc->rate_correction_factors[INTER_NORMAL] *= 0.9;
    }
  }
  return resize_now;
}
