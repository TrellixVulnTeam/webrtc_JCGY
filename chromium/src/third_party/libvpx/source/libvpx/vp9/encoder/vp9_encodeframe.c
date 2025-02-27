/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "./vp9_rtcd.h"
#include "./vpx_dsp_rtcd.h"
#include "./vpx_config.h"

#include "vpx_ports/mem.h"
#include "vpx_ports/vpx_timer.h"

#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_entropy.h"
#include "vp9/common/vp9_entropymode.h"
#include "vp9/common/vp9_idct.h"
#include "vp9/common/vp9_mvref_common.h"
#include "vp9/common/vp9_pred_common.h"
#include "vp9/common/vp9_quant_common.h"
#include "vp9/common/vp9_reconintra.h"
#include "vp9/common/vp9_reconinter.h"
#include "vp9/common/vp9_seg_common.h"
#include "vp9/common/vp9_systemdependent.h"
#include "vp9/common/vp9_tile_common.h"

#include "vp9/encoder/vp9_aq_complexity.h"
#include "vp9/encoder/vp9_aq_cyclicrefresh.h"
#include "vp9/encoder/vp9_aq_variance.h"
#include "vp9/encoder/vp9_encodeframe.h"
#include "vp9/encoder/vp9_encodemb.h"
#include "vp9/encoder/vp9_encodemv.h"
#include "vp9/encoder/vp9_ethread.h"
#include "vp9/encoder/vp9_extend.h"
#include "vp9/encoder/vp9_pickmode.h"
#include "vp9/encoder/vp9_rd.h"
#include "vp9/encoder/vp9_rdopt.h"
#include "vp9/encoder/vp9_segmentation.h"
#include "vp9/encoder/vp9_tokenize.h"

static void encode_superblock(VP9_COMP *cpi, ThreadData * td,
                              TOKENEXTRA **t, int output_enabled,
                              int mi_row, int mi_col, BLOCK_SIZE bsize,
                              PICK_MODE_CONTEXT *ctx);

// This is used as a reference when computing the source variance for the
//  purposes of activity masking.
// Eventually this should be replaced by custom no-reference routines,
//  which will be faster.
static const uint8_t VP9_VAR_OFFS[64] = {
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128
};

#if CONFIG_VP9_HIGHBITDEPTH
static const uint16_t VP9_HIGH_VAR_OFFS_8[64] = {
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128,
    128, 128, 128, 128, 128, 128, 128, 128
};

static const uint16_t VP9_HIGH_VAR_OFFS_10[64] = {
    128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4,
    128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4,
    128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4,
    128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4,
    128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4,
    128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4,
    128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4,
    128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4, 128*4
};

static const uint16_t VP9_HIGH_VAR_OFFS_12[64] = {
    128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16,
    128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16,
    128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16,
    128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16,
    128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16,
    128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16,
    128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16,
    128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16, 128*16
};
#endif  // CONFIG_VP9_HIGHBITDEPTH

unsigned int vp9_get_sby_perpixel_variance(VP9_COMP *cpi,
                                           const struct buf_2d *ref,
                                           BLOCK_SIZE bs) {
  unsigned int sse;
  const unsigned int var = cpi->fn_ptr[bs].vf(ref->buf, ref->stride,
                                              VP9_VAR_OFFS, 0, &sse);
  return ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[bs]);
}

#if CONFIG_VP9_HIGHBITDEPTH
unsigned int vp9_high_get_sby_perpixel_variance(
    VP9_COMP *cpi, const struct buf_2d *ref, BLOCK_SIZE bs, int bd) {
  unsigned int var, sse;
  switch (bd) {
    case 10:
      var = cpi->fn_ptr[bs].vf(ref->buf, ref->stride,
                               CONVERT_TO_BYTEPTR(VP9_HIGH_VAR_OFFS_10),
                               0, &sse);
      break;
    case 12:
      var = cpi->fn_ptr[bs].vf(ref->buf, ref->stride,
                               CONVERT_TO_BYTEPTR(VP9_HIGH_VAR_OFFS_12),
                               0, &sse);
      break;
    case 8:
    default:
      var = cpi->fn_ptr[bs].vf(ref->buf, ref->stride,
                               CONVERT_TO_BYTEPTR(VP9_HIGH_VAR_OFFS_8),
                               0, &sse);
      break;
  }
  return ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[bs]);
}
#endif  // CONFIG_VP9_HIGHBITDEPTH

static unsigned int get_sby_perpixel_diff_variance(VP9_COMP *cpi,
                                                   const struct buf_2d *ref,
                                                   int mi_row, int mi_col,
                                                   BLOCK_SIZE bs) {
  unsigned int sse, var;
  uint8_t *last_y;
  const YV12_BUFFER_CONFIG *last = get_ref_frame_buffer(cpi, LAST_FRAME);

  assert(last != NULL);
  last_y =
      &last->y_buffer[mi_row * MI_SIZE * last->y_stride + mi_col * MI_SIZE];
  var = cpi->fn_ptr[bs].vf(ref->buf, ref->stride, last_y, last->y_stride, &sse);
  return ROUND_POWER_OF_TWO(var, num_pels_log2_lookup[bs]);
}

static BLOCK_SIZE get_rd_var_based_fixed_partition(VP9_COMP *cpi, MACROBLOCK *x,
                                                   int mi_row,
                                                   int mi_col) {
  unsigned int var = get_sby_perpixel_diff_variance(cpi, &x->plane[0].src,
                                                    mi_row, mi_col,
                                                    BLOCK_64X64);
  if (var < 8)
    return BLOCK_64X64;
  else if (var < 128)
    return BLOCK_32X32;
  else if (var < 2048)
    return BLOCK_16X16;
  else
    return BLOCK_8X8;
}

// Lighter version of set_offsets that only sets the mode info
// pointers.
static INLINE void set_mode_info_offsets(VP9_COMMON *const cm,
                                         MACROBLOCK *const x,
                                         MACROBLOCKD *const xd,
                                         int mi_row,
                                         int mi_col) {
  const int idx_str = xd->mi_stride * mi_row + mi_col;
  xd->mi = cm->mi_grid_visible + idx_str;
  xd->mi[0] = cm->mi + idx_str;
  x->mbmi_ext = x->mbmi_ext_base + (mi_row * cm->mi_cols + mi_col);
}

static void set_offsets(VP9_COMP *cpi, const TileInfo *const tile,
                        MACROBLOCK *const x, int mi_row, int mi_col,
                        BLOCK_SIZE bsize) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi;
  const int mi_width = num_8x8_blocks_wide_lookup[bsize];
  const int mi_height = num_8x8_blocks_high_lookup[bsize];
  const struct segmentation *const seg = &cm->seg;

  set_skip_context(xd, mi_row, mi_col);

  set_mode_info_offsets(cm, x, xd, mi_row, mi_col);

  mbmi = &xd->mi[0]->mbmi;

  // Set up destination pointers.
  vp9_setup_dst_planes(xd->plane, get_frame_new_buffer(cm), mi_row, mi_col);

  // Set up limit values for MV components.
  // Mv beyond the range do not produce new/different prediction block.
  x->mv_row_min = -(((mi_row + mi_height) * MI_SIZE) + VP9_INTERP_EXTEND);
  x->mv_col_min = -(((mi_col + mi_width) * MI_SIZE) + VP9_INTERP_EXTEND);
  x->mv_row_max = (cm->mi_rows - mi_row) * MI_SIZE + VP9_INTERP_EXTEND;
  x->mv_col_max = (cm->mi_cols - mi_col) * MI_SIZE + VP9_INTERP_EXTEND;

  // Set up distance of MB to edge of frame in 1/8th pel units.
  assert(!(mi_col & (mi_width - 1)) && !(mi_row & (mi_height - 1)));
  set_mi_row_col(xd, tile, mi_row, mi_height, mi_col, mi_width,
                 cm->mi_rows, cm->mi_cols);

  // Set up source buffers.
  vp9_setup_src_planes(x, cpi->Source, mi_row, mi_col);

  // R/D setup.
  x->rddiv = cpi->rd.RDDIV;
  x->rdmult = cpi->rd.RDMULT;

  // Setup segment ID.
  if (seg->enabled) {
    if (cpi->oxcf.aq_mode != VARIANCE_AQ) {
      const uint8_t *const map = seg->update_map ? cpi->segmentation_map
                                                 : cm->last_frame_seg_map;
      mbmi->segment_id = get_segment_id(cm, map, bsize, mi_row, mi_col);
    }
    vp9_init_plane_quantizers(cpi, x);

    x->encode_breakout = cpi->segment_encode_breakout[mbmi->segment_id];
  } else {
    mbmi->segment_id = 0;
    x->encode_breakout = cpi->encode_breakout;
  }

  // required by vp9_append_sub8x8_mvs_for_idx() and vp9_find_best_ref_mvs()
  xd->tile = *tile;
}

static void duplicate_mode_info_in_sb(VP9_COMMON *cm, MACROBLOCKD *xd,
                                      int mi_row, int mi_col,
                                      BLOCK_SIZE bsize) {
  const int block_width = num_8x8_blocks_wide_lookup[bsize];
  const int block_height = num_8x8_blocks_high_lookup[bsize];
  int i, j;
  for (j = 0; j < block_height; ++j)
    for (i = 0; i < block_width; ++i) {
      if (mi_row + j < cm->mi_rows && mi_col + i < cm->mi_cols)
        xd->mi[j * xd->mi_stride + i] = xd->mi[0];
    }
}

static void set_block_size(VP9_COMP * const cpi,
                           MACROBLOCK *const x,
                           MACROBLOCKD *const xd,
                           int mi_row, int mi_col,
                           BLOCK_SIZE bsize) {
  if (cpi->common.mi_cols > mi_col && cpi->common.mi_rows > mi_row) {
    set_mode_info_offsets(&cpi->common, x, xd, mi_row, mi_col);
    xd->mi[0]->mbmi.sb_type = bsize;
  }
}

typedef struct {
  int64_t sum_square_error;
  int64_t sum_error;
  int log2_count;
  int variance;
} var;

typedef struct {
  var none;
  var horz[2];
  var vert[2];
} partition_variance;

typedef struct {
  partition_variance part_variances;
  var split[4];
} v4x4;

typedef struct {
  partition_variance part_variances;
  v4x4 split[4];
} v8x8;

typedef struct {
  partition_variance part_variances;
  v8x8 split[4];
} v16x16;

typedef struct {
  partition_variance part_variances;
  v16x16 split[4];
} v32x32;

typedef struct {
  partition_variance part_variances;
  v32x32 split[4];
} v64x64;

typedef struct {
  partition_variance *part_variances;
  var *split[4];
} variance_node;

typedef enum {
  V16X16,
  V32X32,
  V64X64,
} TREE_LEVEL;

static void tree_to_node(void *data, BLOCK_SIZE bsize, variance_node *node) {
  int i;
  node->part_variances = NULL;
  switch (bsize) {
    case BLOCK_64X64: {
      v64x64 *vt = (v64x64 *) data;
      node->part_variances = &vt->part_variances;
      for (i = 0; i < 4; i++)
        node->split[i] = &vt->split[i].part_variances.none;
      break;
    }
    case BLOCK_32X32: {
      v32x32 *vt = (v32x32 *) data;
      node->part_variances = &vt->part_variances;
      for (i = 0; i < 4; i++)
        node->split[i] = &vt->split[i].part_variances.none;
      break;
    }
    case BLOCK_16X16: {
      v16x16 *vt = (v16x16 *) data;
      node->part_variances = &vt->part_variances;
      for (i = 0; i < 4; i++)
        node->split[i] = &vt->split[i].part_variances.none;
      break;
    }
    case BLOCK_8X8: {
      v8x8 *vt = (v8x8 *) data;
      node->part_variances = &vt->part_variances;
      for (i = 0; i < 4; i++)
        node->split[i] = &vt->split[i].part_variances.none;
      break;
    }
    case BLOCK_4X4: {
      v4x4 *vt = (v4x4 *) data;
      node->part_variances = &vt->part_variances;
      for (i = 0; i < 4; i++)
        node->split[i] = &vt->split[i];
      break;
    }
    default: {
      assert(0);
      break;
    }
  }
}

// Set variance values given sum square error, sum error, count.
static void fill_variance(int64_t s2, int64_t s, int c, var *v) {
  v->sum_square_error = s2;
  v->sum_error = s;
  v->log2_count = c;
}

static void get_variance(var *v) {
  v->variance = (int)(256 * (v->sum_square_error -
      ((v->sum_error * v->sum_error) >> v->log2_count)) >> v->log2_count);
}

static void sum_2_variances(const var *a, const var *b, var *r) {
  assert(a->log2_count == b->log2_count);
  fill_variance(a->sum_square_error + b->sum_square_error,
                a->sum_error + b->sum_error, a->log2_count + 1, r);
}

static void fill_variance_tree(void *data, BLOCK_SIZE bsize) {
  variance_node node;
  memset(&node, 0, sizeof(node));
  tree_to_node(data, bsize, &node);
  sum_2_variances(node.split[0], node.split[1], &node.part_variances->horz[0]);
  sum_2_variances(node.split[2], node.split[3], &node.part_variances->horz[1]);
  sum_2_variances(node.split[0], node.split[2], &node.part_variances->vert[0]);
  sum_2_variances(node.split[1], node.split[3], &node.part_variances->vert[1]);
  sum_2_variances(&node.part_variances->vert[0], &node.part_variances->vert[1],
                  &node.part_variances->none);
}

static int set_vt_partitioning(VP9_COMP *cpi,
                               MACROBLOCK *const x,
                               MACROBLOCKD *const xd,
                               void *data,
                               BLOCK_SIZE bsize,
                               int mi_row,
                               int mi_col,
                               int64_t threshold,
                               BLOCK_SIZE bsize_min,
                               int force_split) {
  VP9_COMMON * const cm = &cpi->common;
  variance_node vt;
  const int block_width = num_8x8_blocks_wide_lookup[bsize];
  const int block_height = num_8x8_blocks_high_lookup[bsize];
  const int low_res = (cm->width <= 352 && cm->height <= 288);

  assert(block_height == block_width);
  tree_to_node(data, bsize, &vt);

  if (force_split == 1)
    return 0;

  // For bsize=bsize_min (16x16/8x8 for 8x8/4x4 downsampling), select if
  // variance is below threshold, otherwise split will be selected.
  // No check for vert/horiz split as too few samples for variance.
  if (bsize == bsize_min) {
    // Variance already computed to set the force_split.
    if (low_res || cm->frame_type == KEY_FRAME)
      get_variance(&vt.part_variances->none);
    if (mi_col + block_width / 2 < cm->mi_cols &&
        mi_row + block_height / 2 < cm->mi_rows &&
        vt.part_variances->none.variance < threshold) {
      set_block_size(cpi, x, xd, mi_row, mi_col, bsize);
      return 1;
    }
    return 0;
  } else if (bsize > bsize_min) {
    // Variance already computed to set the force_split.
    if (low_res || cm->frame_type == KEY_FRAME)
      get_variance(&vt.part_variances->none);
    // For key frame: take split for bsize above 32X32 or very high variance.
    if (cm->frame_type == KEY_FRAME &&
        (bsize > BLOCK_32X32 ||
        vt.part_variances->none.variance > (threshold << 4))) {
      return 0;
    }
    // If variance is low, take the bsize (no split).
    if (mi_col + block_width / 2 < cm->mi_cols &&
        mi_row + block_height / 2 < cm->mi_rows &&
        vt.part_variances->none.variance < threshold) {
      set_block_size(cpi, x, xd, mi_row, mi_col, bsize);
      return 1;
    }

    // Check vertical split.
    if (mi_row + block_height / 2 < cm->mi_rows) {
      BLOCK_SIZE subsize = get_subsize(bsize, PARTITION_VERT);
      get_variance(&vt.part_variances->vert[0]);
      get_variance(&vt.part_variances->vert[1]);
      if (vt.part_variances->vert[0].variance < threshold &&
          vt.part_variances->vert[1].variance < threshold &&
          get_plane_block_size(subsize, &xd->plane[1]) < BLOCK_INVALID) {
        set_block_size(cpi, x, xd, mi_row, mi_col, subsize);
        set_block_size(cpi, x, xd, mi_row, mi_col + block_width / 2, subsize);
        return 1;
      }
    }
    // Check horizontal split.
    if (mi_col + block_width / 2 < cm->mi_cols) {
      BLOCK_SIZE subsize = get_subsize(bsize, PARTITION_HORZ);
      get_variance(&vt.part_variances->horz[0]);
      get_variance(&vt.part_variances->horz[1]);
      if (vt.part_variances->horz[0].variance < threshold &&
          vt.part_variances->horz[1].variance < threshold &&
          get_plane_block_size(subsize, &xd->plane[1]) < BLOCK_INVALID) {
        set_block_size(cpi, x, xd, mi_row, mi_col, subsize);
        set_block_size(cpi, x, xd, mi_row + block_height / 2, mi_col, subsize);
        return 1;
      }
    }

    return 0;
  }
  return 0;
}

// Set the variance split thresholds for following the block sizes:
// 0 - threshold_64x64, 1 - threshold_32x32, 2 - threshold_16x16,
// 3 - vbp_threshold_8x8. vbp_threshold_8x8 (to split to 4x4 partition) is
// currently only used on key frame.
static void set_vbp_thresholds(VP9_COMP *cpi, int64_t thresholds[], int q) {
  VP9_COMMON *const cm = &cpi->common;
  const int is_key_frame = (cm->frame_type == KEY_FRAME);
  const int threshold_multiplier = is_key_frame ? 20 : 1;
  const int64_t threshold_base = (int64_t)(threshold_multiplier *
      cpi->y_dequant[q][1]);
  if (is_key_frame) {
    thresholds[0] = threshold_base;
    thresholds[1] = threshold_base >> 2;
    thresholds[2] = threshold_base >> 2;
    thresholds[3] = threshold_base << 2;
  } else {
    thresholds[1] = threshold_base;
    if (cm->width <= 352 && cm->height <= 288) {
      thresholds[0] = threshold_base >> 2;
      thresholds[2] = threshold_base << 3;
    } else {
      thresholds[0] = threshold_base;
      thresholds[1] = (5 * threshold_base) >> 2;
      if (cm->width >= 1920 && cm->height >= 1080)
        thresholds[1] = (7 * threshold_base) >> 2;
      thresholds[2] = threshold_base << cpi->oxcf.speed;
    }
  }
}

void vp9_set_variance_partition_thresholds(VP9_COMP *cpi, int q) {
  VP9_COMMON *const cm = &cpi->common;
  SPEED_FEATURES *const sf = &cpi->sf;
  const int is_key_frame = (cm->frame_type == KEY_FRAME);
  if (sf->partition_search_type != VAR_BASED_PARTITION &&
      sf->partition_search_type != REFERENCE_PARTITION) {
    return;
  } else {
    set_vbp_thresholds(cpi, cpi->vbp_thresholds, q);
    // The thresholds below are not changed locally.
    if (is_key_frame) {
      cpi->vbp_threshold_sad = 0;
      cpi->vbp_bsize_min = BLOCK_8X8;
    } else {
      if (cm->width <= 352 && cm->height <= 288)
        cpi->vbp_threshold_sad = 100;
      else
        cpi->vbp_threshold_sad = (cpi->y_dequant[q][1] << 1) > 1000 ?
            (cpi->y_dequant[q][1] << 1) : 1000;
      cpi->vbp_bsize_min = BLOCK_16X16;
    }
    cpi->vbp_threshold_minmax = 15 + (q >> 3);
  }
}

// Compute the minmax over the 8x8 subblocks.
static int compute_minmax_8x8(const uint8_t *s, int sp, const uint8_t *d,
                              int dp, int x16_idx, int y16_idx,
#if CONFIG_VP9_HIGHBITDEPTH
                              int highbd_flag,
#endif
                              int pixels_wide,
                              int pixels_high) {
  int k;
  int minmax_max = 0;
  int minmax_min = 255;
  // Loop over the 4 8x8 subblocks.
  for (k = 0; k < 4; k++) {
    int x8_idx = x16_idx + ((k & 1) << 3);
    int y8_idx = y16_idx + ((k >> 1) << 3);
    int min = 0;
    int max = 0;
    if (x8_idx < pixels_wide && y8_idx < pixels_high) {
#if CONFIG_VP9_HIGHBITDEPTH
      if (highbd_flag & YV12_FLAG_HIGHBITDEPTH) {
        vp9_highbd_minmax_8x8(s + y8_idx * sp + x8_idx, sp,
                              d + y8_idx * dp + x8_idx, dp,
                              &min, &max);
      } else {
        vp9_minmax_8x8(s + y8_idx * sp + x8_idx, sp,
                       d + y8_idx * dp + x8_idx, dp,
                       &min, &max);
      }
#else
      vp9_minmax_8x8(s + y8_idx * sp + x8_idx, sp,
                     d + y8_idx * dp + x8_idx, dp,
                     &min, &max);
#endif
      if ((max - min) > minmax_max)
        minmax_max = (max - min);
      if ((max - min) < minmax_min)
        minmax_min = (max - min);
    }
  }
  return (minmax_max - minmax_min);
}

static void fill_variance_4x4avg(const uint8_t *s, int sp, const uint8_t *d,
                                 int dp, int x8_idx, int y8_idx, v8x8 *vst,
#if CONFIG_VP9_HIGHBITDEPTH
                                 int highbd_flag,
#endif
                                 int pixels_wide,
                                 int pixels_high,
                                 int is_key_frame) {
  int k;
  for (k = 0; k < 4; k++) {
    int x4_idx = x8_idx + ((k & 1) << 2);
    int y4_idx = y8_idx + ((k >> 1) << 2);
    unsigned int sse = 0;
    int sum = 0;
    if (x4_idx < pixels_wide && y4_idx < pixels_high) {
      int s_avg;
      int d_avg = 128;
#if CONFIG_VP9_HIGHBITDEPTH
      if (highbd_flag & YV12_FLAG_HIGHBITDEPTH) {
        s_avg = vp9_highbd_avg_4x4(s + y4_idx * sp + x4_idx, sp);
        if (!is_key_frame)
          d_avg = vp9_highbd_avg_4x4(d + y4_idx * dp + x4_idx, dp);
      } else {
        s_avg = vp9_avg_4x4(s + y4_idx * sp + x4_idx, sp);
        if (!is_key_frame)
          d_avg = vp9_avg_4x4(d + y4_idx * dp + x4_idx, dp);
      }
#else
      s_avg = vp9_avg_4x4(s + y4_idx * sp + x4_idx, sp);
      if (!is_key_frame)
        d_avg = vp9_avg_4x4(d + y4_idx * dp + x4_idx, dp);
#endif
      sum = s_avg - d_avg;
      sse = sum * sum;
    }
    fill_variance(sse, sum, 0, &vst->split[k].part_variances.none);
  }
}

static void fill_variance_8x8avg(const uint8_t *s, int sp, const uint8_t *d,
                                 int dp, int x16_idx, int y16_idx, v16x16 *vst,
#if CONFIG_VP9_HIGHBITDEPTH
                                 int highbd_flag,
#endif
                                 int pixels_wide,
                                 int pixels_high,
                                 int is_key_frame) {
  int k;
  for (k = 0; k < 4; k++) {
    int x8_idx = x16_idx + ((k & 1) << 3);
    int y8_idx = y16_idx + ((k >> 1) << 3);
    unsigned int sse = 0;
    int sum = 0;
    if (x8_idx < pixels_wide && y8_idx < pixels_high) {
      int s_avg;
      int d_avg = 128;
#if CONFIG_VP9_HIGHBITDEPTH
      if (highbd_flag & YV12_FLAG_HIGHBITDEPTH) {
        s_avg = vp9_highbd_avg_8x8(s + y8_idx * sp + x8_idx, sp);
        if (!is_key_frame)
          d_avg = vp9_highbd_avg_8x8(d + y8_idx * dp + x8_idx, dp);
      } else {
        s_avg = vp9_avg_8x8(s + y8_idx * sp + x8_idx, sp);
        if (!is_key_frame)
          d_avg = vp9_avg_8x8(d + y8_idx * dp + x8_idx, dp);
      }
#else
      s_avg = vp9_avg_8x8(s + y8_idx * sp + x8_idx, sp);
      if (!is_key_frame)
        d_avg = vp9_avg_8x8(d + y8_idx * dp + x8_idx, dp);
#endif
      sum = s_avg - d_avg;
      sse = sum * sum;
    }
    fill_variance(sse, sum, 0, &vst->split[k].part_variances.none);
  }
}

// This function chooses partitioning based on the variance between source and
// reconstructed last, where variance is computed for down-sampled inputs.
static int choose_partitioning(VP9_COMP *cpi,
                                const TileInfo *const tile,
                                MACROBLOCK *x,
                                int mi_row, int mi_col) {
  VP9_COMMON * const cm = &cpi->common;
  MACROBLOCKD *xd = &x->e_mbd;
  int i, j, k, m;
  v64x64 vt;
  v16x16 vt2[16];
  int force_split[21];
  uint8_t *s;
  const uint8_t *d;
  int sp;
  int dp;
  int pixels_wide = 64, pixels_high = 64;
  int64_t thresholds[4] = {cpi->vbp_thresholds[0], cpi->vbp_thresholds[1],
      cpi->vbp_thresholds[2], cpi->vbp_thresholds[3]};

  // Always use 4x4 partition for key frame.
  const int is_key_frame = (cm->frame_type == KEY_FRAME);
  const int use_4x4_partition = is_key_frame;
  const int low_res = (cm->width <= 352 && cm->height <= 288);
  int variance4x4downsample[16];

  int segment_id = CR_SEGMENT_ID_BASE;
  if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ && cm->seg.enabled) {
    const uint8_t *const map = cm->seg.update_map ? cpi->segmentation_map :
                                                    cm->last_frame_seg_map;
    segment_id = get_segment_id(cm, map, BLOCK_64X64, mi_row, mi_col);

    if (cyclic_refresh_segment_id_boosted(segment_id)) {
      int q = vp9_get_qindex(&cm->seg, segment_id, cm->base_qindex);
      set_vbp_thresholds(cpi, thresholds, q);
    }
  }

  set_offsets(cpi, tile, x, mi_row, mi_col, BLOCK_64X64);

  if (xd->mb_to_right_edge < 0)
    pixels_wide += (xd->mb_to_right_edge >> 3);
  if (xd->mb_to_bottom_edge < 0)
    pixels_high += (xd->mb_to_bottom_edge >> 3);

  s = x->plane[0].src.buf;
  sp = x->plane[0].src.stride;

  if (!is_key_frame && !(is_one_pass_cbr_svc(cpi) &&
      cpi->svc.layer_context[cpi->svc.temporal_layer_id].is_key_frame)) {
    // In the case of spatial/temporal scalable coding, the assumption here is
    // that the temporal reference frame will always be of type LAST_FRAME.
    // TODO(marpan): If that assumption is broken, we need to revisit this code.
    MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
    unsigned int uv_sad;
    const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_buffer(cpi, LAST_FRAME);

    const YV12_BUFFER_CONFIG *yv12_g = NULL;
    unsigned int y_sad, y_sad_g;
    const BLOCK_SIZE bsize = BLOCK_32X32
        + (mi_col + 4 < cm->mi_cols) * 2 + (mi_row + 4 < cm->mi_rows);

    assert(yv12 != NULL);

    if (!(is_one_pass_cbr_svc(cpi) && cpi->svc.spatial_layer_id)) {
      // For now, GOLDEN will not be used for non-zero spatial layers, since
      // it may not be a temporal reference.
      yv12_g = get_ref_frame_buffer(cpi, GOLDEN_FRAME);
    }

    if (yv12_g && yv12_g != yv12) {
      vp9_setup_pre_planes(xd, 0, yv12_g, mi_row, mi_col,
                           &cm->frame_refs[GOLDEN_FRAME - 1].sf);
      y_sad_g = cpi->fn_ptr[bsize].sdf(x->plane[0].src.buf,
                                       x->plane[0].src.stride,
                                       xd->plane[0].pre[0].buf,
                                       xd->plane[0].pre[0].stride);
    } else {
      y_sad_g = UINT_MAX;
    }

    vp9_setup_pre_planes(xd, 0, yv12, mi_row, mi_col,
                         &cm->frame_refs[LAST_FRAME - 1].sf);
    mbmi->ref_frame[0] = LAST_FRAME;
    mbmi->ref_frame[1] = NONE;
    mbmi->sb_type = BLOCK_64X64;
    mbmi->mv[0].as_int = 0;
    mbmi->interp_filter = BILINEAR;

    y_sad = vp9_int_pro_motion_estimation(cpi, x, bsize, mi_row, mi_col);
    if (y_sad_g < y_sad) {
      vp9_setup_pre_planes(xd, 0, yv12_g, mi_row, mi_col,
                           &cm->frame_refs[GOLDEN_FRAME - 1].sf);
      mbmi->ref_frame[0] = GOLDEN_FRAME;
      mbmi->mv[0].as_int = 0;
      y_sad = y_sad_g;
    } else {
      x->pred_mv[LAST_FRAME] = mbmi->mv[0].as_mv;
    }

    vp9_build_inter_predictors_sb(xd, mi_row, mi_col, BLOCK_64X64);

    for (i = 1; i <= 2; ++i) {
      struct macroblock_plane  *p = &x->plane[i];
      struct macroblockd_plane *pd = &xd->plane[i];
      const BLOCK_SIZE bs = get_plane_block_size(bsize, pd);

      if (bs == BLOCK_INVALID)
        uv_sad = UINT_MAX;
      else
        uv_sad = cpi->fn_ptr[bs].sdf(p->src.buf, p->src.stride,
                                     pd->dst.buf, pd->dst.stride);

      x->color_sensitivity[i - 1] = uv_sad > (y_sad >> 2);
    }

    d = xd->plane[0].dst.buf;
    dp = xd->plane[0].dst.stride;

    // If the y_sad is very small, take 64x64 as partition and exit.
    // Don't check on boosted segment for now, as 64x64 is suppressed there.
    if (segment_id == CR_SEGMENT_ID_BASE &&
        y_sad < cpi->vbp_threshold_sad) {
      const int block_width = num_8x8_blocks_wide_lookup[BLOCK_64X64];
      const int block_height = num_8x8_blocks_high_lookup[BLOCK_64X64];
      if (mi_col + block_width / 2 < cm->mi_cols &&
          mi_row + block_height / 2 < cm->mi_rows) {
        set_block_size(cpi, x, xd, mi_row, mi_col, BLOCK_64X64);
        return 0;
      }
    }
  } else {
    d = VP9_VAR_OFFS;
    dp = 0;
#if CONFIG_VP9_HIGHBITDEPTH
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
      switch (xd->bd) {
        case 10:
          d = CONVERT_TO_BYTEPTR(VP9_HIGH_VAR_OFFS_10);
          break;
        case 12:
          d = CONVERT_TO_BYTEPTR(VP9_HIGH_VAR_OFFS_12);
          break;
        case 8:
        default:
          d = CONVERT_TO_BYTEPTR(VP9_HIGH_VAR_OFFS_8);
          break;
      }
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH
  }

  // Index for force_split: 0 for 64x64, 1-4 for 32x32 blocks,
  // 5-20 for the 16x16 blocks.
  force_split[0] = 0;
  // Fill in the entire tree of 8x8 (or 4x4 under some conditions) variances
  // for splits.
  for (i = 0; i < 4; i++) {
    const int x32_idx = ((i & 1) << 5);
    const int y32_idx = ((i >> 1) << 5);
    const int i2 = i << 2;
    force_split[i + 1] = 0;
    for (j = 0; j < 4; j++) {
      const int x16_idx = x32_idx + ((j & 1) << 4);
      const int y16_idx = y32_idx + ((j >> 1) << 4);
      const int split_index = 5 + i2 + j;
      v16x16 *vst = &vt.split[i].split[j];
      force_split[split_index] = 0;
      variance4x4downsample[i2 + j] = 0;
      if (!is_key_frame) {
        fill_variance_8x8avg(s, sp, d, dp, x16_idx, y16_idx, vst,
#if CONFIG_VP9_HIGHBITDEPTH
                            xd->cur_buf->flags,
#endif
                            pixels_wide,
                            pixels_high,
                            is_key_frame);
        fill_variance_tree(&vt.split[i].split[j], BLOCK_16X16);
        get_variance(&vt.split[i].split[j].part_variances.none);
        if (vt.split[i].split[j].part_variances.none.variance >
            thresholds[2]) {
          // 16X16 variance is above threshold for split, so force split to 8x8
          // for this 16x16 block (this also forces splits for upper levels).
          force_split[split_index] = 1;
          force_split[i + 1] = 1;
          force_split[0] = 1;
        } else if (vt.split[i].split[j].part_variances.none.variance >
                   thresholds[1] &&
                   !cyclic_refresh_segment_id_boosted(segment_id)) {
          // We have some nominal amount of 16x16 variance (based on average),
          // compute the minmax over the 8x8 sub-blocks, and if above threshold,
          // force split to 8x8 block for this 16x16 block.
          int minmax = compute_minmax_8x8(s, sp, d, dp, x16_idx, y16_idx,
#if CONFIG_VP9_HIGHBITDEPTH
                                          xd->cur_buf->flags,
#endif
                                          pixels_wide, pixels_high);
          if (minmax > cpi->vbp_threshold_minmax) {
            force_split[split_index] = 1;
            force_split[i + 1] = 1;
            force_split[0] = 1;
          }
        }
      }
      // TODO(marpan): There is an issue with variance based on 4x4 average in
      // svc mode, don't allow it for now.
      if (is_key_frame || (low_res && !cpi->use_svc &&
          vt.split[i].split[j].part_variances.none.variance >
          (thresholds[1] << 1))) {
        force_split[split_index] = 0;
        // Go down to 4x4 down-sampling for variance.
        variance4x4downsample[i2 + j] = 1;
        for (k = 0; k < 4; k++) {
          int x8_idx = x16_idx + ((k & 1) << 3);
          int y8_idx = y16_idx + ((k >> 1) << 3);
          v8x8 *vst2 = is_key_frame ? &vst->split[k] :
              &vt2[i2 + j].split[k];
          fill_variance_4x4avg(s, sp, d, dp, x8_idx, y8_idx, vst2,
#if CONFIG_VP9_HIGHBITDEPTH
                               xd->cur_buf->flags,
#endif
                               pixels_wide,
                               pixels_high,
                               is_key_frame);
        }
      }
    }
  }

  // Fill the rest of the variance tree by summing split partition values.
  for (i = 0; i < 4; i++) {
    const int i2 = i << 2;
    for (j = 0; j < 4; j++) {
      if (variance4x4downsample[i2 + j] == 1) {
        v16x16 *vtemp = (!is_key_frame) ? &vt2[i2 + j] :
            &vt.split[i].split[j];
        for (m = 0; m < 4; m++)
          fill_variance_tree(&vtemp->split[m], BLOCK_8X8);
        fill_variance_tree(vtemp, BLOCK_16X16);
      }
    }
    fill_variance_tree(&vt.split[i], BLOCK_32X32);
    // If variance of this 32x32 block is above the threshold, force the block
    // to split. This also forces a split on the upper (64x64) level.
    if (!force_split[i + 1]) {
      get_variance(&vt.split[i].part_variances.none);
      if (vt.split[i].part_variances.none.variance > thresholds[1]) {
        force_split[i + 1] = 1;
        force_split[0] = 1;
      }
    }
  }
  if (!force_split[0]) {
    fill_variance_tree(&vt, BLOCK_64X64);
    get_variance(&vt.part_variances.none);
  }

  // Now go through the entire structure, splitting every block size until
  // we get to one that's got a variance lower than our threshold.
  if ( mi_col + 8 > cm->mi_cols || mi_row + 8 > cm->mi_rows ||
      !set_vt_partitioning(cpi, x, xd, &vt, BLOCK_64X64, mi_row, mi_col,
                           thresholds[0], BLOCK_16X16, force_split[0])) {
    for (i = 0; i < 4; ++i) {
      const int x32_idx = ((i & 1) << 2);
      const int y32_idx = ((i >> 1) << 2);
      const int i2 = i << 2;
      if (!set_vt_partitioning(cpi, x, xd, &vt.split[i], BLOCK_32X32,
                               (mi_row + y32_idx), (mi_col + x32_idx),
                               thresholds[1], BLOCK_16X16,
                               force_split[i + 1])) {
        for (j = 0; j < 4; ++j) {
          const int x16_idx = ((j & 1) << 1);
          const int y16_idx = ((j >> 1) << 1);
          // For inter frames: if variance4x4downsample[] == 1 for this 16x16
          // block, then the variance is based on 4x4 down-sampling, so use vt2
          // in set_vt_partioning(), otherwise use vt.
          v16x16 *vtemp = (!is_key_frame &&
                           variance4x4downsample[i2 + j] == 1) ?
                           &vt2[i2 + j] : &vt.split[i].split[j];
          if (!set_vt_partitioning(cpi, x, xd, vtemp, BLOCK_16X16,
                                   mi_row + y32_idx + y16_idx,
                                   mi_col + x32_idx + x16_idx,
                                   thresholds[2],
                                   cpi->vbp_bsize_min,
                                   force_split[5 + i2  + j])) {
            for (k = 0; k < 4; ++k) {
              const int x8_idx = (k & 1);
              const int y8_idx = (k >> 1);
              if (use_4x4_partition) {
                if (!set_vt_partitioning(cpi, x, xd, &vtemp->split[k],
                                         BLOCK_8X8,
                                         mi_row + y32_idx + y16_idx + y8_idx,
                                         mi_col + x32_idx + x16_idx + x8_idx,
                                         thresholds[3], BLOCK_8X8, 0)) {
                  set_block_size(cpi, x, xd,
                                 (mi_row + y32_idx + y16_idx + y8_idx),
                                 (mi_col + x32_idx + x16_idx + x8_idx),
                                 BLOCK_4X4);
                }
              } else {
                set_block_size(cpi, x, xd,
                               (mi_row + y32_idx + y16_idx + y8_idx),
                               (mi_col + x32_idx + x16_idx + x8_idx),
                               BLOCK_8X8);
              }
            }
          }
        }
      }
    }
  }
  return 0;
}

static void update_state(VP9_COMP *cpi, ThreadData *td,
                         PICK_MODE_CONTEXT *ctx,
                         int mi_row, int mi_col, BLOCK_SIZE bsize,
                         int output_enabled) {
  int i, x_idx, y;
  VP9_COMMON *const cm = &cpi->common;
  RD_COUNTS *const rdc = &td->rd_counts;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = x->plane;
  struct macroblockd_plane *const pd = xd->plane;
  MODE_INFO *mi = &ctx->mic;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  MODE_INFO *mi_addr = xd->mi[0];
  const struct segmentation *const seg = &cm->seg;
  const int bw = num_8x8_blocks_wide_lookup[mi->mbmi.sb_type];
  const int bh = num_8x8_blocks_high_lookup[mi->mbmi.sb_type];
  const int x_mis = MIN(bw, cm->mi_cols - mi_col);
  const int y_mis = MIN(bh, cm->mi_rows - mi_row);
  MV_REF *const frame_mvs =
      cm->cur_frame->mvs + mi_row * cm->mi_cols + mi_col;
  int w, h;

  const int mis = cm->mi_stride;
  const int mi_width = num_8x8_blocks_wide_lookup[bsize];
  const int mi_height = num_8x8_blocks_high_lookup[bsize];
  int max_plane;

  assert(mi->mbmi.sb_type == bsize);

  *mi_addr = *mi;
  *x->mbmi_ext = ctx->mbmi_ext;

  // If segmentation in use
  if (seg->enabled) {
    // For in frame complexity AQ copy the segment id from the segment map.
    if (cpi->oxcf.aq_mode == COMPLEXITY_AQ) {
      const uint8_t *const map = seg->update_map ? cpi->segmentation_map
                                                 : cm->last_frame_seg_map;
      mi_addr->mbmi.segment_id =
        get_segment_id(cm, map, bsize, mi_row, mi_col);
    }
    // Else for cyclic refresh mode update the segment map, set the segment id
    // and then update the quantizer.
    if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ) {
      vp9_cyclic_refresh_update_segment(cpi, &xd->mi[0]->mbmi, mi_row,
                                        mi_col, bsize, ctx->rate, ctx->dist,
                                        x->skip);
    }
  }

  max_plane = is_inter_block(mbmi) ? MAX_MB_PLANE : 1;
  for (i = 0; i < max_plane; ++i) {
    p[i].coeff = ctx->coeff_pbuf[i][1];
    p[i].qcoeff = ctx->qcoeff_pbuf[i][1];
    pd[i].dqcoeff = ctx->dqcoeff_pbuf[i][1];
    p[i].eobs = ctx->eobs_pbuf[i][1];
  }

  for (i = max_plane; i < MAX_MB_PLANE; ++i) {
    p[i].coeff = ctx->coeff_pbuf[i][2];
    p[i].qcoeff = ctx->qcoeff_pbuf[i][2];
    pd[i].dqcoeff = ctx->dqcoeff_pbuf[i][2];
    p[i].eobs = ctx->eobs_pbuf[i][2];
  }

  // Restore the coding context of the MB to that that was in place
  // when the mode was picked for it
  for (y = 0; y < mi_height; y++)
    for (x_idx = 0; x_idx < mi_width; x_idx++)
      if ((xd->mb_to_right_edge >> (3 + MI_SIZE_LOG2)) + mi_width > x_idx
        && (xd->mb_to_bottom_edge >> (3 + MI_SIZE_LOG2)) + mi_height > y) {
        xd->mi[x_idx + y * mis] = mi_addr;
      }

  if (cpi->oxcf.aq_mode)
    vp9_init_plane_quantizers(cpi, x);

  if (is_inter_block(mbmi) && mbmi->sb_type < BLOCK_8X8) {
    mbmi->mv[0].as_int = mi->bmi[3].as_mv[0].as_int;
    mbmi->mv[1].as_int = mi->bmi[3].as_mv[1].as_int;
  }

  x->skip = ctx->skip;
  memcpy(x->zcoeff_blk[mbmi->tx_size], ctx->zcoeff_blk,
         sizeof(uint8_t) * ctx->num_4x4_blk);

  if (!output_enabled)
    return;

#if CONFIG_INTERNAL_STATS
  if (frame_is_intra_only(cm)) {
    static const int kf_mode_index[] = {
      THR_DC        /*DC_PRED*/,
      THR_V_PRED    /*V_PRED*/,
      THR_H_PRED    /*H_PRED*/,
      THR_D45_PRED  /*D45_PRED*/,
      THR_D135_PRED /*D135_PRED*/,
      THR_D117_PRED /*D117_PRED*/,
      THR_D153_PRED /*D153_PRED*/,
      THR_D207_PRED /*D207_PRED*/,
      THR_D63_PRED  /*D63_PRED*/,
      THR_TM        /*TM_PRED*/,
    };
    ++cpi->mode_chosen_counts[kf_mode_index[mbmi->mode]];
  } else {
    // Note how often each mode chosen as best
    ++cpi->mode_chosen_counts[ctx->best_mode_index];
  }
#endif
  if (!frame_is_intra_only(cm)) {
    if (is_inter_block(mbmi)) {
      vp9_update_mv_count(td);

      if (cm->interp_filter == SWITCHABLE) {
        const int ctx = vp9_get_pred_context_switchable_interp(xd);
        ++td->counts->switchable_interp[ctx][mbmi->interp_filter];
      }
    }

    rdc->comp_pred_diff[SINGLE_REFERENCE] += ctx->single_pred_diff;
    rdc->comp_pred_diff[COMPOUND_REFERENCE] += ctx->comp_pred_diff;
    rdc->comp_pred_diff[REFERENCE_MODE_SELECT] += ctx->hybrid_pred_diff;

    for (i = 0; i < SWITCHABLE_FILTER_CONTEXTS; ++i)
      rdc->filter_diff[i] += ctx->best_filter_diff[i];
  }

  for (h = 0; h < y_mis; ++h) {
    MV_REF *const frame_mv = frame_mvs + h * cm->mi_cols;
    for (w = 0; w < x_mis; ++w) {
      MV_REF *const mv = frame_mv + w;
      mv->ref_frame[0] = mi->mbmi.ref_frame[0];
      mv->ref_frame[1] = mi->mbmi.ref_frame[1];
      mv->mv[0].as_int = mi->mbmi.mv[0].as_int;
      mv->mv[1].as_int = mi->mbmi.mv[1].as_int;
    }
  }
}

void vp9_setup_src_planes(MACROBLOCK *x, const YV12_BUFFER_CONFIG *src,
                          int mi_row, int mi_col) {
  uint8_t *const buffers[3] = {src->y_buffer, src->u_buffer, src->v_buffer };
  const int strides[3] = {src->y_stride, src->uv_stride, src->uv_stride };
  int i;

  // Set current frame pointer.
  x->e_mbd.cur_buf = src;

  for (i = 0; i < MAX_MB_PLANE; i++)
    setup_pred_plane(&x->plane[i].src, buffers[i], strides[i], mi_row, mi_col,
                     NULL, x->e_mbd.plane[i].subsampling_x,
                     x->e_mbd.plane[i].subsampling_y);
}

static void set_mode_info_seg_skip(MACROBLOCK *x, TX_MODE tx_mode,
                                   RD_COST *rd_cost, BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  INTERP_FILTER filter_ref;

  if (xd->up_available)
    filter_ref = xd->mi[-xd->mi_stride]->mbmi.interp_filter;
  else if (xd->left_available)
    filter_ref = xd->mi[-1]->mbmi.interp_filter;
  else
    filter_ref = EIGHTTAP;

  mbmi->sb_type = bsize;
  mbmi->mode = ZEROMV;
  mbmi->tx_size = MIN(max_txsize_lookup[bsize],
                      tx_mode_to_biggest_tx_size[tx_mode]);
  mbmi->skip = 1;
  mbmi->uv_mode = DC_PRED;
  mbmi->ref_frame[0] = LAST_FRAME;
  mbmi->ref_frame[1] = NONE;
  mbmi->mv[0].as_int = 0;
  mbmi->interp_filter = filter_ref;

  xd->mi[0]->bmi[0].as_mv[0].as_int = 0;
  x->skip = 1;

  vp9_rd_cost_init(rd_cost);
}

static int set_segment_rdmult(VP9_COMP *const cpi,
                               MACROBLOCK *const x,
                               int8_t segment_id) {
  int segment_qindex;
  VP9_COMMON *const cm = &cpi->common;
  vp9_init_plane_quantizers(cpi, x);
  vp9_clear_system_state();
  segment_qindex = vp9_get_qindex(&cm->seg, segment_id,
                                  cm->base_qindex);
  return vp9_compute_rd_mult(cpi, segment_qindex + cm->y_dc_delta_q);
}

static void rd_pick_sb_modes(VP9_COMP *cpi,
                             TileDataEnc *tile_data,
                             MACROBLOCK *const x,
                             int mi_row, int mi_col, RD_COST *rd_cost,
                             BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx,
                             int64_t best_rd) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi;
  struct macroblock_plane *const p = x->plane;
  struct macroblockd_plane *const pd = xd->plane;
  const AQ_MODE aq_mode = cpi->oxcf.aq_mode;
  int i, orig_rdmult;

  vp9_clear_system_state();

  // Use the lower precision, but faster, 32x32 fdct for mode selection.
  x->use_lp32x32fdct = 1;

  set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);
  mbmi = &xd->mi[0]->mbmi;
  mbmi->sb_type = bsize;

  for (i = 0; i < MAX_MB_PLANE; ++i) {
    p[i].coeff = ctx->coeff_pbuf[i][0];
    p[i].qcoeff = ctx->qcoeff_pbuf[i][0];
    pd[i].dqcoeff = ctx->dqcoeff_pbuf[i][0];
    p[i].eobs = ctx->eobs_pbuf[i][0];
  }
  ctx->is_coded = 0;
  ctx->skippable = 0;
  ctx->pred_pixel_ready = 0;
  x->skip_recode = 0;

  // Set to zero to make sure we do not use the previous encoded frame stats
  mbmi->skip = 0;

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    x->source_variance =
        vp9_high_get_sby_perpixel_variance(cpi, &x->plane[0].src,
                                           bsize, xd->bd);
  } else {
    x->source_variance =
      vp9_get_sby_perpixel_variance(cpi, &x->plane[0].src, bsize);
  }
#else
  x->source_variance =
    vp9_get_sby_perpixel_variance(cpi, &x->plane[0].src, bsize);
#endif  // CONFIG_VP9_HIGHBITDEPTH

  // Save rdmult before it might be changed, so it can be restored later.
  orig_rdmult = x->rdmult;

  if (aq_mode == VARIANCE_AQ) {
    const int energy = bsize <= BLOCK_16X16 ? x->mb_energy
                                            : vp9_block_energy(cpi, x, bsize);
    if (cm->frame_type == KEY_FRAME ||
        cpi->refresh_alt_ref_frame ||
        (cpi->refresh_golden_frame && !cpi->rc.is_src_frame_alt_ref)) {
      mbmi->segment_id = vp9_vaq_segment_id(energy);
    } else {
      const uint8_t *const map = cm->seg.update_map ? cpi->segmentation_map
                                                    : cm->last_frame_seg_map;
      mbmi->segment_id = get_segment_id(cm, map, bsize, mi_row, mi_col);
    }
    x->rdmult = set_segment_rdmult(cpi, x, mbmi->segment_id);
  } else if (aq_mode == COMPLEXITY_AQ) {
    x->rdmult = set_segment_rdmult(cpi, x, mbmi->segment_id);
  } else if (aq_mode == CYCLIC_REFRESH_AQ) {
    const uint8_t *const map = cm->seg.update_map ? cpi->segmentation_map
                                                  : cm->last_frame_seg_map;
    // If segment is boosted, use rdmult for that segment.
    if (cyclic_refresh_segment_id_boosted(
            get_segment_id(cm, map, bsize, mi_row, mi_col)))
      x->rdmult = vp9_cyclic_refresh_get_rdmult(cpi->cyclic_refresh);
  }

  // Find best coding mode & reconstruct the MB so it is available
  // as a predictor for MBs that follow in the SB
  if (frame_is_intra_only(cm)) {
    vp9_rd_pick_intra_mode_sb(cpi, x, rd_cost, bsize, ctx, best_rd);
  } else {
    if (bsize >= BLOCK_8X8) {
      if (segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP))
        vp9_rd_pick_inter_mode_sb_seg_skip(cpi, tile_data, x, rd_cost, bsize,
                                           ctx, best_rd);
      else
        vp9_rd_pick_inter_mode_sb(cpi, tile_data, x, mi_row, mi_col,
                                  rd_cost, bsize, ctx, best_rd);
    } else {
      vp9_rd_pick_inter_mode_sub8x8(cpi, tile_data, x, mi_row, mi_col,
                                    rd_cost, bsize, ctx, best_rd);
    }
  }


  // Examine the resulting rate and for AQ mode 2 make a segment choice.
  if ((rd_cost->rate != INT_MAX) &&
      (aq_mode == COMPLEXITY_AQ) && (bsize >= BLOCK_16X16) &&
      (cm->frame_type == KEY_FRAME ||
       cpi->refresh_alt_ref_frame ||
       (cpi->refresh_golden_frame && !cpi->rc.is_src_frame_alt_ref))) {
    vp9_caq_select_segment(cpi, x, bsize, mi_row, mi_col, rd_cost->rate);
  }

  x->rdmult = orig_rdmult;

  // TODO(jingning) The rate-distortion optimization flow needs to be
  // refactored to provide proper exit/return handle.
  if (rd_cost->rate == INT_MAX)
    rd_cost->rdcost = INT64_MAX;

  ctx->rate = rd_cost->rate;
  ctx->dist = rd_cost->dist;
}

static void update_stats(VP9_COMMON *cm, ThreadData *td) {
  const MACROBLOCK *x = &td->mb;
  const MACROBLOCKD *const xd = &x->e_mbd;
  const MODE_INFO *const mi = xd->mi[0];
  const MB_MODE_INFO *const mbmi = &mi->mbmi;
  const MB_MODE_INFO_EXT *const mbmi_ext = x->mbmi_ext;
  const BLOCK_SIZE bsize = mbmi->sb_type;

  if (!frame_is_intra_only(cm)) {
    FRAME_COUNTS *const counts = td->counts;
    const int inter_block = is_inter_block(mbmi);
    const int seg_ref_active = segfeature_active(&cm->seg, mbmi->segment_id,
                                                 SEG_LVL_REF_FRAME);
    if (!seg_ref_active) {
      counts->intra_inter[vp9_get_intra_inter_context(xd)][inter_block]++;
      // If the segment reference feature is enabled we have only a single
      // reference frame allowed for the segment so exclude it from
      // the reference frame counts used to work out probabilities.
      if (inter_block) {
        const MV_REFERENCE_FRAME ref0 = mbmi->ref_frame[0];
        if (cm->reference_mode == REFERENCE_MODE_SELECT)
          counts->comp_inter[vp9_get_reference_mode_context(cm, xd)]
                            [has_second_ref(mbmi)]++;

        if (has_second_ref(mbmi)) {
          counts->comp_ref[vp9_get_pred_context_comp_ref_p(cm, xd)]
                          [ref0 == GOLDEN_FRAME]++;
        } else {
          counts->single_ref[vp9_get_pred_context_single_ref_p1(xd)][0]
                            [ref0 != LAST_FRAME]++;
          if (ref0 != LAST_FRAME)
            counts->single_ref[vp9_get_pred_context_single_ref_p2(xd)][1]
                              [ref0 != GOLDEN_FRAME]++;
        }
      }
    }
    if (inter_block &&
        !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP)) {
      const int mode_ctx = mbmi_ext->mode_context[mbmi->ref_frame[0]];
      if (bsize >= BLOCK_8X8) {
        const PREDICTION_MODE mode = mbmi->mode;
        ++counts->inter_mode[mode_ctx][INTER_OFFSET(mode)];
      } else {
        const int num_4x4_w = num_4x4_blocks_wide_lookup[bsize];
        const int num_4x4_h = num_4x4_blocks_high_lookup[bsize];
        int idx, idy;
        for (idy = 0; idy < 2; idy += num_4x4_h) {
          for (idx = 0; idx < 2; idx += num_4x4_w) {
            const int j = idy * 2 + idx;
            const PREDICTION_MODE b_mode = mi->bmi[j].as_mode;
            ++counts->inter_mode[mode_ctx][INTER_OFFSET(b_mode)];
          }
        }
      }
    }
  }
}

static void restore_context(MACROBLOCK *const x, int mi_row, int mi_col,
                            ENTROPY_CONTEXT a[16 * MAX_MB_PLANE],
                            ENTROPY_CONTEXT l[16 * MAX_MB_PLANE],
                            PARTITION_CONTEXT sa[8], PARTITION_CONTEXT sl[8],
                            BLOCK_SIZE bsize) {
  MACROBLOCKD *const xd = &x->e_mbd;
  int p;
  const int num_4x4_blocks_wide = num_4x4_blocks_wide_lookup[bsize];
  const int num_4x4_blocks_high = num_4x4_blocks_high_lookup[bsize];
  int mi_width = num_8x8_blocks_wide_lookup[bsize];
  int mi_height = num_8x8_blocks_high_lookup[bsize];
  for (p = 0; p < MAX_MB_PLANE; p++) {
    memcpy(
        xd->above_context[p] + ((mi_col * 2) >> xd->plane[p].subsampling_x),
        a + num_4x4_blocks_wide * p,
        (sizeof(ENTROPY_CONTEXT) * num_4x4_blocks_wide) >>
        xd->plane[p].subsampling_x);
    memcpy(
        xd->left_context[p]
            + ((mi_row & MI_MASK) * 2 >> xd->plane[p].subsampling_y),
        l + num_4x4_blocks_high * p,
        (sizeof(ENTROPY_CONTEXT) * num_4x4_blocks_high) >>
        xd->plane[p].subsampling_y);
  }
  memcpy(xd->above_seg_context + mi_col, sa,
         sizeof(*xd->above_seg_context) * mi_width);
  memcpy(xd->left_seg_context + (mi_row & MI_MASK), sl,
         sizeof(xd->left_seg_context[0]) * mi_height);
}

static void save_context(MACROBLOCK *const x, int mi_row, int mi_col,
                         ENTROPY_CONTEXT a[16 * MAX_MB_PLANE],
                         ENTROPY_CONTEXT l[16 * MAX_MB_PLANE],
                         PARTITION_CONTEXT sa[8], PARTITION_CONTEXT sl[8],
                         BLOCK_SIZE bsize) {
  const MACROBLOCKD *const xd = &x->e_mbd;
  int p;
  const int num_4x4_blocks_wide = num_4x4_blocks_wide_lookup[bsize];
  const int num_4x4_blocks_high = num_4x4_blocks_high_lookup[bsize];
  int mi_width = num_8x8_blocks_wide_lookup[bsize];
  int mi_height = num_8x8_blocks_high_lookup[bsize];

  // buffer the above/left context information of the block in search.
  for (p = 0; p < MAX_MB_PLANE; ++p) {
    memcpy(
        a + num_4x4_blocks_wide * p,
        xd->above_context[p] + (mi_col * 2 >> xd->plane[p].subsampling_x),
        (sizeof(ENTROPY_CONTEXT) * num_4x4_blocks_wide) >>
        xd->plane[p].subsampling_x);
    memcpy(
        l + num_4x4_blocks_high * p,
        xd->left_context[p]
            + ((mi_row & MI_MASK) * 2 >> xd->plane[p].subsampling_y),
        (sizeof(ENTROPY_CONTEXT) * num_4x4_blocks_high) >>
        xd->plane[p].subsampling_y);
  }
  memcpy(sa, xd->above_seg_context + mi_col,
         sizeof(*xd->above_seg_context) * mi_width);
  memcpy(sl, xd->left_seg_context + (mi_row & MI_MASK),
         sizeof(xd->left_seg_context[0]) * mi_height);
}

static void encode_b(VP9_COMP *cpi, const TileInfo *const tile,
                     ThreadData *td,
                     TOKENEXTRA **tp, int mi_row, int mi_col,
                     int output_enabled, BLOCK_SIZE bsize,
                     PICK_MODE_CONTEXT *ctx) {
  MACROBLOCK *const x = &td->mb;
  set_offsets(cpi, tile, x, mi_row, mi_col, bsize);
  update_state(cpi, td, ctx, mi_row, mi_col, bsize, output_enabled);
  encode_superblock(cpi, td, tp, output_enabled, mi_row, mi_col, bsize, ctx);

  if (output_enabled) {
    update_stats(&cpi->common, td);

    (*tp)->token = EOSB_TOKEN;
    (*tp)++;
  }
}

static void encode_sb(VP9_COMP *cpi, ThreadData *td,
                      const TileInfo *const tile,
                      TOKENEXTRA **tp, int mi_row, int mi_col,
                      int output_enabled, BLOCK_SIZE bsize,
                      PC_TREE *pc_tree) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;

  const int bsl = b_width_log2_lookup[bsize], hbs = (1 << bsl) / 4;
  int ctx;
  PARTITION_TYPE partition;
  BLOCK_SIZE subsize = bsize;

  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  if (bsize >= BLOCK_8X8) {
    ctx = partition_plane_context(xd, mi_row, mi_col, bsize);
    subsize = get_subsize(bsize, pc_tree->partitioning);
  } else {
    ctx = 0;
    subsize = BLOCK_4X4;
  }

  partition = partition_lookup[bsl][subsize];
  if (output_enabled && bsize != BLOCK_4X4)
    td->counts->partition[ctx][partition]++;

  switch (partition) {
    case PARTITION_NONE:
      encode_b(cpi, tile, td, tp, mi_row, mi_col, output_enabled, subsize,
               &pc_tree->none);
      break;
    case PARTITION_VERT:
      encode_b(cpi, tile, td, tp, mi_row, mi_col, output_enabled, subsize,
               &pc_tree->vertical[0]);
      if (mi_col + hbs < cm->mi_cols && bsize > BLOCK_8X8) {
        encode_b(cpi, tile, td, tp, mi_row, mi_col + hbs, output_enabled,
                 subsize, &pc_tree->vertical[1]);
      }
      break;
    case PARTITION_HORZ:
      encode_b(cpi, tile, td, tp, mi_row, mi_col, output_enabled, subsize,
               &pc_tree->horizontal[0]);
      if (mi_row + hbs < cm->mi_rows && bsize > BLOCK_8X8) {
        encode_b(cpi, tile, td, tp, mi_row + hbs, mi_col, output_enabled,
                 subsize, &pc_tree->horizontal[1]);
      }
      break;
    case PARTITION_SPLIT:
      if (bsize == BLOCK_8X8) {
        encode_b(cpi, tile, td, tp, mi_row, mi_col, output_enabled, subsize,
                 pc_tree->leaf_split[0]);
      } else {
        encode_sb(cpi, td, tile, tp, mi_row, mi_col, output_enabled, subsize,
                  pc_tree->split[0]);
        encode_sb(cpi, td, tile, tp, mi_row, mi_col + hbs, output_enabled,
                  subsize, pc_tree->split[1]);
        encode_sb(cpi, td, tile, tp, mi_row + hbs, mi_col, output_enabled,
                  subsize, pc_tree->split[2]);
        encode_sb(cpi, td, tile, tp, mi_row + hbs, mi_col + hbs, output_enabled,
                  subsize, pc_tree->split[3]);
      }
      break;
    default:
      assert(0 && "Invalid partition type.");
      break;
  }

  if (partition != PARTITION_SPLIT || bsize == BLOCK_8X8)
    update_partition_context(xd, mi_row, mi_col, subsize, bsize);
}

// Check to see if the given partition size is allowed for a specified number
// of 8x8 block rows and columns remaining in the image.
// If not then return the largest allowed partition size
static BLOCK_SIZE find_partition_size(BLOCK_SIZE bsize,
                                      int rows_left, int cols_left,
                                      int *bh, int *bw) {
  if (rows_left <= 0 || cols_left <= 0) {
    return MIN(bsize, BLOCK_8X8);
  } else {
    for (; bsize > 0; bsize -= 3) {
      *bh = num_8x8_blocks_high_lookup[bsize];
      *bw = num_8x8_blocks_wide_lookup[bsize];
      if ((*bh <= rows_left) && (*bw <= cols_left)) {
        break;
      }
    }
  }
  return bsize;
}

static void set_partial_b64x64_partition(MODE_INFO *mi, int mis,
    int bh_in, int bw_in, int row8x8_remaining, int col8x8_remaining,
    BLOCK_SIZE bsize, MODE_INFO **mi_8x8) {
  int bh = bh_in;
  int r, c;
  for (r = 0; r < MI_BLOCK_SIZE; r += bh) {
    int bw = bw_in;
    for (c = 0; c < MI_BLOCK_SIZE; c += bw) {
      const int index = r * mis + c;
      mi_8x8[index] = mi + index;
      mi_8x8[index]->mbmi.sb_type = find_partition_size(bsize,
          row8x8_remaining - r, col8x8_remaining - c, &bh, &bw);
    }
  }
}

// This function attempts to set all mode info entries in a given SB64
// to the same block partition size.
// However, at the bottom and right borders of the image the requested size
// may not be allowed in which case this code attempts to choose the largest
// allowable partition.
static void set_fixed_partitioning(VP9_COMP *cpi, const TileInfo *const tile,
                                   MODE_INFO **mi_8x8, int mi_row, int mi_col,
                                   BLOCK_SIZE bsize) {
  VP9_COMMON *const cm = &cpi->common;
  const int mis = cm->mi_stride;
  const int row8x8_remaining = tile->mi_row_end - mi_row;
  const int col8x8_remaining = tile->mi_col_end - mi_col;
  int block_row, block_col;
  MODE_INFO *mi_upper_left = cm->mi + mi_row * mis + mi_col;
  int bh = num_8x8_blocks_high_lookup[bsize];
  int bw = num_8x8_blocks_wide_lookup[bsize];

  assert((row8x8_remaining > 0) && (col8x8_remaining > 0));

  // Apply the requested partition size to the SB64 if it is all "in image"
  if ((col8x8_remaining >= MI_BLOCK_SIZE) &&
      (row8x8_remaining >= MI_BLOCK_SIZE)) {
    for (block_row = 0; block_row < MI_BLOCK_SIZE; block_row += bh) {
      for (block_col = 0; block_col < MI_BLOCK_SIZE; block_col += bw) {
        int index = block_row * mis + block_col;
        mi_8x8[index] = mi_upper_left + index;
        mi_8x8[index]->mbmi.sb_type = bsize;
      }
    }
  } else {
    // Else this is a partial SB64.
    set_partial_b64x64_partition(mi_upper_left, mis, bh, bw, row8x8_remaining,
        col8x8_remaining, bsize, mi_8x8);
  }
}

static const struct {
  int row;
  int col;
} coord_lookup[16] = {
    // 32x32 index = 0
    {0, 0}, {0, 2}, {2, 0}, {2, 2},
    // 32x32 index = 1
    {0, 4}, {0, 6}, {2, 4}, {2, 6},
    // 32x32 index = 2
    {4, 0}, {4, 2}, {6, 0}, {6, 2},
    // 32x32 index = 3
    {4, 4}, {4, 6}, {6, 4}, {6, 6},
};

static void set_source_var_based_partition(VP9_COMP *cpi,
                                           const TileInfo *const tile,
                                           MACROBLOCK *const x,
                                           MODE_INFO **mi_8x8,
                                           int mi_row, int mi_col) {
  VP9_COMMON *const cm = &cpi->common;
  const int mis = cm->mi_stride;
  const int row8x8_remaining = tile->mi_row_end - mi_row;
  const int col8x8_remaining = tile->mi_col_end - mi_col;
  MODE_INFO *mi_upper_left = cm->mi + mi_row * mis + mi_col;

  vp9_setup_src_planes(x, cpi->Source, mi_row, mi_col);

  assert((row8x8_remaining > 0) && (col8x8_remaining > 0));

  // In-image SB64
  if ((col8x8_remaining >= MI_BLOCK_SIZE) &&
      (row8x8_remaining >= MI_BLOCK_SIZE)) {
    int i, j;
    int index;
    diff d32[4];
    const int offset = (mi_row >> 1) * cm->mb_cols + (mi_col >> 1);
    int is_larger_better = 0;
    int use32x32 = 0;
    unsigned int thr = cpi->source_var_thresh;

    memset(d32, 0, 4 * sizeof(diff));

    for (i = 0; i < 4; i++) {
      diff *d16[4];

      for (j = 0; j < 4; j++) {
        int b_mi_row = coord_lookup[i * 4 + j].row;
        int b_mi_col = coord_lookup[i * 4 + j].col;
        int boffset = b_mi_row / 2 * cm->mb_cols +
                      b_mi_col / 2;

        d16[j] = cpi->source_diff_var + offset + boffset;

        index = b_mi_row * mis + b_mi_col;
        mi_8x8[index] = mi_upper_left + index;
        mi_8x8[index]->mbmi.sb_type = BLOCK_16X16;

        // TODO(yunqingwang): If d16[j].var is very large, use 8x8 partition
        // size to further improve quality.
      }

      is_larger_better = (d16[0]->var < thr) && (d16[1]->var < thr) &&
          (d16[2]->var < thr) && (d16[3]->var < thr);

      // Use 32x32 partition
      if (is_larger_better) {
        use32x32 += 1;

        for (j = 0; j < 4; j++) {
          d32[i].sse += d16[j]->sse;
          d32[i].sum += d16[j]->sum;
        }

        d32[i].var = d32[i].sse - (((int64_t)d32[i].sum * d32[i].sum) >> 10);

        index = coord_lookup[i*4].row * mis + coord_lookup[i*4].col;
        mi_8x8[index] = mi_upper_left + index;
        mi_8x8[index]->mbmi.sb_type = BLOCK_32X32;
      }
    }

    if (use32x32 == 4) {
      thr <<= 1;
      is_larger_better = (d32[0].var < thr) && (d32[1].var < thr) &&
          (d32[2].var < thr) && (d32[3].var < thr);

      // Use 64x64 partition
      if (is_larger_better) {
        mi_8x8[0] = mi_upper_left;
        mi_8x8[0]->mbmi.sb_type = BLOCK_64X64;
      }
    }
  } else {   // partial in-image SB64
    int bh = num_8x8_blocks_high_lookup[BLOCK_16X16];
    int bw = num_8x8_blocks_wide_lookup[BLOCK_16X16];
    set_partial_b64x64_partition(mi_upper_left, mis, bh, bw,
        row8x8_remaining, col8x8_remaining, BLOCK_16X16, mi_8x8);
  }
}

static void update_state_rt(VP9_COMP *cpi, ThreadData *td,
                            PICK_MODE_CONTEXT *ctx,
                            int mi_row, int mi_col, int bsize) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO *const mi = xd->mi[0];
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  const struct segmentation *const seg = &cm->seg;
  const int bw = num_8x8_blocks_wide_lookup[mi->mbmi.sb_type];
  const int bh = num_8x8_blocks_high_lookup[mi->mbmi.sb_type];
  const int x_mis = MIN(bw, cm->mi_cols - mi_col);
  const int y_mis = MIN(bh, cm->mi_rows - mi_row);

  *(xd->mi[0]) = ctx->mic;
  *(x->mbmi_ext) = ctx->mbmi_ext;

  if (seg->enabled && cpi->oxcf.aq_mode) {
    // For in frame complexity AQ or variance AQ, copy segment_id from
    // segmentation_map.
    if (cpi->oxcf.aq_mode == COMPLEXITY_AQ ||
        cpi->oxcf.aq_mode == VARIANCE_AQ ) {
      const uint8_t *const map = seg->update_map ? cpi->segmentation_map
                                                 : cm->last_frame_seg_map;
      mbmi->segment_id = get_segment_id(cm, map, bsize, mi_row, mi_col);
    } else {
    // Setting segmentation map for cyclic_refresh.
      vp9_cyclic_refresh_update_segment(cpi, mbmi, mi_row, mi_col, bsize,
                                        ctx->rate, ctx->dist, x->skip);
    }
    vp9_init_plane_quantizers(cpi, x);
  }

  if (is_inter_block(mbmi)) {
    vp9_update_mv_count(td);
    if (cm->interp_filter == SWITCHABLE) {
      const int pred_ctx = vp9_get_pred_context_switchable_interp(xd);
      ++td->counts->switchable_interp[pred_ctx][mbmi->interp_filter];
    }

    if (mbmi->sb_type < BLOCK_8X8) {
      mbmi->mv[0].as_int = mi->bmi[3].as_mv[0].as_int;
      mbmi->mv[1].as_int = mi->bmi[3].as_mv[1].as_int;
    }
  }

  if (cm->use_prev_frame_mvs) {
    MV_REF *const frame_mvs =
        cm->cur_frame->mvs + mi_row * cm->mi_cols + mi_col;
    int w, h;

    for (h = 0; h < y_mis; ++h) {
      MV_REF *const frame_mv = frame_mvs + h * cm->mi_cols;
      for (w = 0; w < x_mis; ++w) {
        MV_REF *const mv = frame_mv + w;
        mv->ref_frame[0] = mi->mbmi.ref_frame[0];
        mv->ref_frame[1] = mi->mbmi.ref_frame[1];
        mv->mv[0].as_int = mi->mbmi.mv[0].as_int;
        mv->mv[1].as_int = mi->mbmi.mv[1].as_int;
      }
    }
  }

  x->skip = ctx->skip;
  x->skip_txfm[0] = mbmi->segment_id ? 0 : ctx->skip_txfm[0];
}

static void encode_b_rt(VP9_COMP *cpi, ThreadData *td,
                        const TileInfo *const tile,
                        TOKENEXTRA **tp, int mi_row, int mi_col,
                        int output_enabled, BLOCK_SIZE bsize,
                        PICK_MODE_CONTEXT *ctx) {
  MACROBLOCK *const x = &td->mb;
  set_offsets(cpi, tile, x, mi_row, mi_col, bsize);
  update_state_rt(cpi, td, ctx, mi_row, mi_col, bsize);

#if CONFIG_VP9_TEMPORAL_DENOISING
  if (cpi->oxcf.noise_sensitivity > 0 && output_enabled &&
      cpi->common.frame_type != KEY_FRAME) {
    vp9_denoiser_denoise(&cpi->denoiser, x, mi_row, mi_col,
                         MAX(BLOCK_8X8, bsize), ctx);
  }
#endif

  encode_superblock(cpi, td, tp, output_enabled, mi_row, mi_col, bsize, ctx);
  update_stats(&cpi->common, td);

  (*tp)->token = EOSB_TOKEN;
  (*tp)++;
}

static void encode_sb_rt(VP9_COMP *cpi, ThreadData *td,
                         const TileInfo *const tile,
                         TOKENEXTRA **tp, int mi_row, int mi_col,
                         int output_enabled, BLOCK_SIZE bsize,
                         PC_TREE *pc_tree) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;

  const int bsl = b_width_log2_lookup[bsize], hbs = (1 << bsl) / 4;
  int ctx;
  PARTITION_TYPE partition;
  BLOCK_SIZE subsize;

  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  if (bsize >= BLOCK_8X8) {
    const int idx_str = xd->mi_stride * mi_row + mi_col;
    MODE_INFO ** mi_8x8 = cm->mi_grid_visible + idx_str;
    ctx = partition_plane_context(xd, mi_row, mi_col, bsize);
    subsize = mi_8x8[0]->mbmi.sb_type;
  } else {
    ctx = 0;
    subsize = BLOCK_4X4;
  }

  partition = partition_lookup[bsl][subsize];
  if (output_enabled && bsize != BLOCK_4X4)
    td->counts->partition[ctx][partition]++;

  switch (partition) {
    case PARTITION_NONE:
      encode_b_rt(cpi, td, tile, tp, mi_row, mi_col, output_enabled, subsize,
                  &pc_tree->none);
      break;
    case PARTITION_VERT:
      encode_b_rt(cpi, td, tile, tp, mi_row, mi_col, output_enabled, subsize,
                  &pc_tree->vertical[0]);
      if (mi_col + hbs < cm->mi_cols && bsize > BLOCK_8X8) {
        encode_b_rt(cpi, td, tile, tp, mi_row, mi_col + hbs, output_enabled,
                    subsize, &pc_tree->vertical[1]);
      }
      break;
    case PARTITION_HORZ:
      encode_b_rt(cpi, td, tile, tp, mi_row, mi_col, output_enabled, subsize,
                  &pc_tree->horizontal[0]);
      if (mi_row + hbs < cm->mi_rows && bsize > BLOCK_8X8) {
        encode_b_rt(cpi, td, tile, tp, mi_row + hbs, mi_col, output_enabled,
                    subsize, &pc_tree->horizontal[1]);
      }
      break;
    case PARTITION_SPLIT:
      subsize = get_subsize(bsize, PARTITION_SPLIT);
      encode_sb_rt(cpi, td, tile, tp, mi_row, mi_col, output_enabled, subsize,
                   pc_tree->split[0]);
      encode_sb_rt(cpi, td, tile, tp, mi_row, mi_col + hbs, output_enabled,
                   subsize, pc_tree->split[1]);
      encode_sb_rt(cpi, td, tile, tp, mi_row + hbs, mi_col, output_enabled,
                   subsize, pc_tree->split[2]);
      encode_sb_rt(cpi, td, tile, tp, mi_row + hbs, mi_col + hbs,
                   output_enabled, subsize, pc_tree->split[3]);
      break;
    default:
      assert(0 && "Invalid partition type.");
      break;
  }

  if (partition != PARTITION_SPLIT || bsize == BLOCK_8X8)
    update_partition_context(xd, mi_row, mi_col, subsize, bsize);
}

static void rd_use_partition(VP9_COMP *cpi,
                             ThreadData *td,
                             TileDataEnc *tile_data,
                             MODE_INFO **mi_8x8, TOKENEXTRA **tp,
                             int mi_row, int mi_col,
                             BLOCK_SIZE bsize,
                             int *rate, int64_t *dist,
                             int do_recon, PC_TREE *pc_tree) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int mis = cm->mi_stride;
  const int bsl = b_width_log2_lookup[bsize];
  const int mi_step = num_4x4_blocks_wide_lookup[bsize] / 2;
  const int bss = (1 << bsl) / 4;
  int i, pl;
  PARTITION_TYPE partition = PARTITION_NONE;
  BLOCK_SIZE subsize;
  ENTROPY_CONTEXT l[16 * MAX_MB_PLANE], a[16 * MAX_MB_PLANE];
  PARTITION_CONTEXT sl[8], sa[8];
  RD_COST last_part_rdc, none_rdc, chosen_rdc;
  BLOCK_SIZE sub_subsize = BLOCK_4X4;
  int splits_below = 0;
  BLOCK_SIZE bs_type = mi_8x8[0]->mbmi.sb_type;
  int do_partition_search = 1;
  PICK_MODE_CONTEXT *ctx = &pc_tree->none;

  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  assert(num_4x4_blocks_wide_lookup[bsize] ==
         num_4x4_blocks_high_lookup[bsize]);

  vp9_rd_cost_reset(&last_part_rdc);
  vp9_rd_cost_reset(&none_rdc);
  vp9_rd_cost_reset(&chosen_rdc);

  partition = partition_lookup[bsl][bs_type];
  subsize = get_subsize(bsize, partition);

  pc_tree->partitioning = partition;
  save_context(x, mi_row, mi_col, a, l, sa, sl, bsize);

  if (bsize == BLOCK_16X16 && cpi->oxcf.aq_mode) {
    set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);
    x->mb_energy = vp9_block_energy(cpi, x, bsize);
  }

  if (do_partition_search &&
      cpi->sf.partition_search_type == SEARCH_PARTITION &&
      cpi->sf.adjust_partitioning_from_last_frame) {
    // Check if any of the sub blocks are further split.
    if (partition == PARTITION_SPLIT && subsize > BLOCK_8X8) {
      sub_subsize = get_subsize(subsize, PARTITION_SPLIT);
      splits_below = 1;
      for (i = 0; i < 4; i++) {
        int jj = i >> 1, ii = i & 0x01;
        MODE_INFO *this_mi = mi_8x8[jj * bss * mis + ii * bss];
        if (this_mi && this_mi->mbmi.sb_type >= sub_subsize) {
          splits_below = 0;
        }
      }
    }

    // If partition is not none try none unless each of the 4 splits are split
    // even further..
    if (partition != PARTITION_NONE && !splits_below &&
        mi_row + (mi_step >> 1) < cm->mi_rows &&
        mi_col + (mi_step >> 1) < cm->mi_cols) {
      pc_tree->partitioning = PARTITION_NONE;
      rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &none_rdc, bsize,
                       ctx, INT64_MAX);

      pl = partition_plane_context(xd, mi_row, mi_col, bsize);

      if (none_rdc.rate < INT_MAX) {
        none_rdc.rate += cpi->partition_cost[pl][PARTITION_NONE];
        none_rdc.rdcost = RDCOST(x->rdmult, x->rddiv, none_rdc.rate,
                                 none_rdc.dist);
      }

      restore_context(x, mi_row, mi_col, a, l, sa, sl, bsize);
      mi_8x8[0]->mbmi.sb_type = bs_type;
      pc_tree->partitioning = partition;
    }
  }

  switch (partition) {
    case PARTITION_NONE:
      rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                       bsize, ctx, INT64_MAX);
      break;
    case PARTITION_HORZ:
      rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                       subsize, &pc_tree->horizontal[0],
                       INT64_MAX);
      if (last_part_rdc.rate != INT_MAX &&
          bsize >= BLOCK_8X8 && mi_row + (mi_step >> 1) < cm->mi_rows) {
        RD_COST tmp_rdc;
        PICK_MODE_CONTEXT *ctx = &pc_tree->horizontal[0];
        vp9_rd_cost_init(&tmp_rdc);
        update_state(cpi, td, ctx, mi_row, mi_col, subsize, 0);
        encode_superblock(cpi, td, tp, 0, mi_row, mi_col, subsize, ctx);
        rd_pick_sb_modes(cpi, tile_data, x,
                         mi_row + (mi_step >> 1), mi_col, &tmp_rdc,
                         subsize, &pc_tree->horizontal[1], INT64_MAX);
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          vp9_rd_cost_reset(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
        last_part_rdc.rdcost += tmp_rdc.rdcost;
      }
      break;
    case PARTITION_VERT:
      rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                       subsize, &pc_tree->vertical[0], INT64_MAX);
      if (last_part_rdc.rate != INT_MAX &&
          bsize >= BLOCK_8X8 && mi_col + (mi_step >> 1) < cm->mi_cols) {
        RD_COST tmp_rdc;
        PICK_MODE_CONTEXT *ctx = &pc_tree->vertical[0];
        vp9_rd_cost_init(&tmp_rdc);
        update_state(cpi, td, ctx, mi_row, mi_col, subsize, 0);
        encode_superblock(cpi, td, tp, 0, mi_row, mi_col, subsize, ctx);
        rd_pick_sb_modes(cpi, tile_data, x,
                         mi_row, mi_col + (mi_step >> 1), &tmp_rdc,
                         subsize, &pc_tree->vertical[bsize > BLOCK_8X8],
                         INT64_MAX);
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          vp9_rd_cost_reset(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
        last_part_rdc.rdcost += tmp_rdc.rdcost;
      }
      break;
    case PARTITION_SPLIT:
      if (bsize == BLOCK_8X8) {
        rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &last_part_rdc,
                         subsize, pc_tree->leaf_split[0], INT64_MAX);
        break;
      }
      last_part_rdc.rate = 0;
      last_part_rdc.dist = 0;
      last_part_rdc.rdcost = 0;
      for (i = 0; i < 4; i++) {
        int x_idx = (i & 1) * (mi_step >> 1);
        int y_idx = (i >> 1) * (mi_step >> 1);
        int jj = i >> 1, ii = i & 0x01;
        RD_COST tmp_rdc;
        if ((mi_row + y_idx >= cm->mi_rows) || (mi_col + x_idx >= cm->mi_cols))
          continue;

        vp9_rd_cost_init(&tmp_rdc);
        rd_use_partition(cpi, td, tile_data,
                         mi_8x8 + jj * bss * mis + ii * bss, tp,
                         mi_row + y_idx, mi_col + x_idx, subsize,
                         &tmp_rdc.rate, &tmp_rdc.dist,
                         i != 3, pc_tree->split[i]);
        if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
          vp9_rd_cost_reset(&last_part_rdc);
          break;
        }
        last_part_rdc.rate += tmp_rdc.rate;
        last_part_rdc.dist += tmp_rdc.dist;
      }
      break;
    default:
      assert(0);
      break;
  }

  pl = partition_plane_context(xd, mi_row, mi_col, bsize);
  if (last_part_rdc.rate < INT_MAX) {
    last_part_rdc.rate += cpi->partition_cost[pl][partition];
    last_part_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                                  last_part_rdc.rate, last_part_rdc.dist);
  }

  if (do_partition_search
      && cpi->sf.adjust_partitioning_from_last_frame
      && cpi->sf.partition_search_type == SEARCH_PARTITION
      && partition != PARTITION_SPLIT && bsize > BLOCK_8X8
      && (mi_row + mi_step < cm->mi_rows ||
          mi_row + (mi_step >> 1) == cm->mi_rows)
      && (mi_col + mi_step < cm->mi_cols ||
          mi_col + (mi_step >> 1) == cm->mi_cols)) {
    BLOCK_SIZE split_subsize = get_subsize(bsize, PARTITION_SPLIT);
    chosen_rdc.rate = 0;
    chosen_rdc.dist = 0;
    restore_context(x, mi_row, mi_col, a, l, sa, sl, bsize);
    pc_tree->partitioning = PARTITION_SPLIT;

    // Split partition.
    for (i = 0; i < 4; i++) {
      int x_idx = (i & 1) * (mi_step >> 1);
      int y_idx = (i >> 1) * (mi_step >> 1);
      RD_COST tmp_rdc;
      ENTROPY_CONTEXT l[16 * MAX_MB_PLANE], a[16 * MAX_MB_PLANE];
      PARTITION_CONTEXT sl[8], sa[8];

      if ((mi_row + y_idx >= cm->mi_rows) || (mi_col + x_idx >= cm->mi_cols))
        continue;

      save_context(x, mi_row, mi_col, a, l, sa, sl, bsize);
      pc_tree->split[i]->partitioning = PARTITION_NONE;
      rd_pick_sb_modes(cpi, tile_data, x,
                       mi_row + y_idx, mi_col + x_idx, &tmp_rdc,
                       split_subsize, &pc_tree->split[i]->none, INT64_MAX);

      restore_context(x, mi_row, mi_col, a, l, sa, sl, bsize);

      if (tmp_rdc.rate == INT_MAX || tmp_rdc.dist == INT64_MAX) {
        vp9_rd_cost_reset(&chosen_rdc);
        break;
      }

      chosen_rdc.rate += tmp_rdc.rate;
      chosen_rdc.dist += tmp_rdc.dist;

      if (i != 3)
        encode_sb(cpi, td, tile_info, tp,  mi_row + y_idx, mi_col + x_idx, 0,
                  split_subsize, pc_tree->split[i]);

      pl = partition_plane_context(xd, mi_row + y_idx, mi_col + x_idx,
                                   split_subsize);
      chosen_rdc.rate += cpi->partition_cost[pl][PARTITION_NONE];
    }
    pl = partition_plane_context(xd, mi_row, mi_col, bsize);
    if (chosen_rdc.rate < INT_MAX) {
      chosen_rdc.rate += cpi->partition_cost[pl][PARTITION_SPLIT];
      chosen_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                                 chosen_rdc.rate, chosen_rdc.dist);
    }
  }

  // If last_part is better set the partitioning to that.
  if (last_part_rdc.rdcost < chosen_rdc.rdcost) {
    mi_8x8[0]->mbmi.sb_type = bsize;
    if (bsize >= BLOCK_8X8)
      pc_tree->partitioning = partition;
    chosen_rdc = last_part_rdc;
  }
  // If none was better set the partitioning to that.
  if (none_rdc.rdcost < chosen_rdc.rdcost) {
    if (bsize >= BLOCK_8X8)
      pc_tree->partitioning = PARTITION_NONE;
    chosen_rdc = none_rdc;
  }

  restore_context(x, mi_row, mi_col, a, l, sa, sl, bsize);

  // We must have chosen a partitioning and encoding or we'll fail later on.
  // No other opportunities for success.
  if (bsize == BLOCK_64X64)
    assert(chosen_rdc.rate < INT_MAX && chosen_rdc.dist < INT64_MAX);

  if (do_recon) {
    int output_enabled = (bsize == BLOCK_64X64);
    encode_sb(cpi, td, tile_info, tp, mi_row, mi_col, output_enabled, bsize,
              pc_tree);
  }

  *rate = chosen_rdc.rate;
  *dist = chosen_rdc.dist;
}

static const BLOCK_SIZE min_partition_size[BLOCK_SIZES] = {
  BLOCK_4X4,   BLOCK_4X4,   BLOCK_4X4,
  BLOCK_4X4,   BLOCK_4X4,   BLOCK_4X4,
  BLOCK_8X8,   BLOCK_8X8,   BLOCK_8X8,
  BLOCK_16X16, BLOCK_16X16, BLOCK_16X16,
  BLOCK_16X16
};

static const BLOCK_SIZE max_partition_size[BLOCK_SIZES] = {
  BLOCK_8X8,   BLOCK_16X16, BLOCK_16X16,
  BLOCK_16X16, BLOCK_32X32, BLOCK_32X32,
  BLOCK_32X32, BLOCK_64X64, BLOCK_64X64,
  BLOCK_64X64, BLOCK_64X64, BLOCK_64X64,
  BLOCK_64X64
};


// Look at all the mode_info entries for blocks that are part of this
// partition and find the min and max values for sb_type.
// At the moment this is designed to work on a 64x64 SB but could be
// adjusted to use a size parameter.
//
// The min and max are assumed to have been initialized prior to calling this
// function so repeat calls can accumulate a min and max of more than one sb64.
static void get_sb_partition_size_range(MACROBLOCKD *xd, MODE_INFO **mi_8x8,
                                        BLOCK_SIZE *min_block_size,
                                        BLOCK_SIZE *max_block_size,
                                        int bs_hist[BLOCK_SIZES]) {
  int sb_width_in_blocks = MI_BLOCK_SIZE;
  int sb_height_in_blocks  = MI_BLOCK_SIZE;
  int i, j;
  int index = 0;

  // Check the sb_type for each block that belongs to this region.
  for (i = 0; i < sb_height_in_blocks; ++i) {
    for (j = 0; j < sb_width_in_blocks; ++j) {
      MODE_INFO *mi = mi_8x8[index+j];
      BLOCK_SIZE sb_type = mi ? mi->mbmi.sb_type : 0;
      bs_hist[sb_type]++;
      *min_block_size = MIN(*min_block_size, sb_type);
      *max_block_size = MAX(*max_block_size, sb_type);
    }
    index += xd->mi_stride;
  }
}

// Next square block size less or equal than current block size.
static const BLOCK_SIZE next_square_size[BLOCK_SIZES] = {
  BLOCK_4X4, BLOCK_4X4, BLOCK_4X4,
  BLOCK_8X8, BLOCK_8X8, BLOCK_8X8,
  BLOCK_16X16, BLOCK_16X16, BLOCK_16X16,
  BLOCK_32X32, BLOCK_32X32, BLOCK_32X32,
  BLOCK_64X64
};

// Look at neighboring blocks and set a min and max partition size based on
// what they chose.
static void rd_auto_partition_range(VP9_COMP *cpi, const TileInfo *const tile,
                                    MACROBLOCKD *const xd,
                                    int mi_row, int mi_col,
                                    BLOCK_SIZE *min_block_size,
                                    BLOCK_SIZE *max_block_size) {
  VP9_COMMON *const cm = &cpi->common;
  MODE_INFO **mi = xd->mi;
  const int left_in_image = xd->left_available && mi[-1];
  const int above_in_image = xd->up_available && mi[-xd->mi_stride];
  const int row8x8_remaining = tile->mi_row_end - mi_row;
  const int col8x8_remaining = tile->mi_col_end - mi_col;
  int bh, bw;
  BLOCK_SIZE min_size = BLOCK_4X4;
  BLOCK_SIZE max_size = BLOCK_64X64;
  int bs_hist[BLOCK_SIZES] = {0};

  // Trap case where we do not have a prediction.
  if (left_in_image || above_in_image || cm->frame_type != KEY_FRAME) {
    // Default "min to max" and "max to min"
    min_size = BLOCK_64X64;
    max_size = BLOCK_4X4;

    // NOTE: each call to get_sb_partition_size_range() uses the previous
    // passed in values for min and max as a starting point.
    // Find the min and max partition used in previous frame at this location
    if (cm->frame_type != KEY_FRAME) {
      MODE_INFO **prev_mi =
          &cm->prev_mi_grid_visible[mi_row * xd->mi_stride + mi_col];
      get_sb_partition_size_range(xd, prev_mi, &min_size, &max_size, bs_hist);
    }
    // Find the min and max partition sizes used in the left SB64
    if (left_in_image) {
      MODE_INFO **left_sb64_mi = &mi[-MI_BLOCK_SIZE];
      get_sb_partition_size_range(xd, left_sb64_mi, &min_size, &max_size,
                                  bs_hist);
    }
    // Find the min and max partition sizes used in the above SB64.
    if (above_in_image) {
      MODE_INFO **above_sb64_mi = &mi[-xd->mi_stride * MI_BLOCK_SIZE];
      get_sb_partition_size_range(xd, above_sb64_mi, &min_size, &max_size,
                                  bs_hist);
    }

    // Adjust observed min and max for "relaxed" auto partition case.
    if (cpi->sf.auto_min_max_partition_size == RELAXED_NEIGHBORING_MIN_MAX) {
      min_size = min_partition_size[min_size];
      max_size = max_partition_size[max_size];
    }
  }

  // Check border cases where max and min from neighbors may not be legal.
  max_size = find_partition_size(max_size,
                                 row8x8_remaining, col8x8_remaining,
                                 &bh, &bw);
  // Test for blocks at the edge of the active image.
  // This may be the actual edge of the image or where there are formatting
  // bars.
  if (vp9_active_edge_sb(cpi, mi_row, mi_col)) {
    min_size = BLOCK_4X4;
  } else {
    min_size = MIN(cpi->sf.rd_auto_partition_min_limit,
                   MIN(min_size, max_size));
  }

  // When use_square_partition_only is true, make sure at least one square
  // partition is allowed by selecting the next smaller square size as
  // *min_block_size.
  if (cpi->sf.use_square_partition_only &&
      next_square_size[max_size] < min_size) {
     min_size = next_square_size[max_size];
  }

  *min_block_size = min_size;
  *max_block_size = max_size;
}

// TODO(jingning) refactor functions setting partition search range
static void set_partition_range(VP9_COMMON *cm, MACROBLOCKD *xd,
                                int mi_row, int mi_col, BLOCK_SIZE bsize,
                                BLOCK_SIZE *min_bs, BLOCK_SIZE *max_bs) {
  int mi_width  = num_8x8_blocks_wide_lookup[bsize];
  int mi_height = num_8x8_blocks_high_lookup[bsize];
  int idx, idy;

  MODE_INFO *mi;
  const int idx_str = cm->mi_stride * mi_row + mi_col;
  MODE_INFO **prev_mi = &cm->prev_mi_grid_visible[idx_str];
  BLOCK_SIZE bs, min_size, max_size;

  min_size = BLOCK_64X64;
  max_size = BLOCK_4X4;

  if (prev_mi) {
    for (idy = 0; idy < mi_height; ++idy) {
      for (idx = 0; idx < mi_width; ++idx) {
        mi = prev_mi[idy * cm->mi_stride + idx];
        bs = mi ? mi->mbmi.sb_type : bsize;
        min_size = MIN(min_size, bs);
        max_size = MAX(max_size, bs);
      }
    }
  }

  if (xd->left_available) {
    for (idy = 0; idy < mi_height; ++idy) {
      mi = xd->mi[idy * cm->mi_stride - 1];
      bs = mi ? mi->mbmi.sb_type : bsize;
      min_size = MIN(min_size, bs);
      max_size = MAX(max_size, bs);
    }
  }

  if (xd->up_available) {
    for (idx = 0; idx < mi_width; ++idx) {
      mi = xd->mi[idx - cm->mi_stride];
      bs = mi ? mi->mbmi.sb_type : bsize;
      min_size = MIN(min_size, bs);
      max_size = MAX(max_size, bs);
    }
  }

  if (min_size == max_size) {
    min_size = min_partition_size[min_size];
    max_size = max_partition_size[max_size];
  }

  *min_bs = min_size;
  *max_bs = max_size;
}

static INLINE void store_pred_mv(MACROBLOCK *x, PICK_MODE_CONTEXT *ctx) {
  memcpy(ctx->pred_mv, x->pred_mv, sizeof(x->pred_mv));
}

static INLINE void load_pred_mv(MACROBLOCK *x, PICK_MODE_CONTEXT *ctx) {
  memcpy(x->pred_mv, ctx->pred_mv, sizeof(x->pred_mv));
}

#if CONFIG_FP_MB_STATS
const int num_16x16_blocks_wide_lookup[BLOCK_SIZES] =
  {1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 4, 4};
const int num_16x16_blocks_high_lookup[BLOCK_SIZES] =
  {1, 1, 1, 1, 1, 1, 1, 2, 1, 2, 4, 2, 4};
const int qindex_skip_threshold_lookup[BLOCK_SIZES] =
  {0, 10, 10, 30, 40, 40, 60, 80, 80, 90, 100, 100, 120};
const int qindex_split_threshold_lookup[BLOCK_SIZES] =
  {0, 3, 3, 7, 15, 15, 30, 40, 40, 60, 80, 80, 120};
const int complexity_16x16_blocks_threshold[BLOCK_SIZES] =
  {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 4, 4, 6};

typedef enum {
  MV_ZERO = 0,
  MV_LEFT = 1,
  MV_UP = 2,
  MV_RIGHT = 3,
  MV_DOWN = 4,
  MV_INVALID
} MOTION_DIRECTION;

static INLINE MOTION_DIRECTION get_motion_direction_fp(uint8_t fp_byte) {
  if (fp_byte & FPMB_MOTION_ZERO_MASK) {
    return MV_ZERO;
  } else if (fp_byte & FPMB_MOTION_LEFT_MASK) {
    return MV_LEFT;
  } else if (fp_byte & FPMB_MOTION_RIGHT_MASK) {
    return MV_RIGHT;
  } else if (fp_byte & FPMB_MOTION_UP_MASK) {
    return MV_UP;
  } else {
    return MV_DOWN;
  }
}

static INLINE int get_motion_inconsistency(MOTION_DIRECTION this_mv,
                                           MOTION_DIRECTION that_mv) {
  if (this_mv == that_mv) {
    return 0;
  } else {
    return abs(this_mv - that_mv) == 2 ? 2 : 1;
  }
}
#endif

// TODO(jingning,jimbankoski,rbultje): properly skip partition types that are
// unlikely to be selected depending on previous rate-distortion optimization
// results, for encoding speed-up.
static void rd_pick_partition(VP9_COMP *cpi, ThreadData *td,
                              TileDataEnc *tile_data,
                              TOKENEXTRA **tp, int mi_row, int mi_col,
                              BLOCK_SIZE bsize, RD_COST *rd_cost,
                              int64_t best_rd, PC_TREE *pc_tree) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int mi_step = num_8x8_blocks_wide_lookup[bsize] / 2;
  ENTROPY_CONTEXT l[16 * MAX_MB_PLANE], a[16 * MAX_MB_PLANE];
  PARTITION_CONTEXT sl[8], sa[8];
  TOKENEXTRA *tp_orig = *tp;
  PICK_MODE_CONTEXT *ctx = &pc_tree->none;
  int i, pl;
  BLOCK_SIZE subsize;
  RD_COST this_rdc, sum_rdc, best_rdc;
  int do_split = bsize >= BLOCK_8X8;
  int do_rect = 1;

  // Override skipping rectangular partition operations for edge blocks
  const int force_horz_split = (mi_row + mi_step >= cm->mi_rows);
  const int force_vert_split = (mi_col + mi_step >= cm->mi_cols);
  const int xss = x->e_mbd.plane[1].subsampling_x;
  const int yss = x->e_mbd.plane[1].subsampling_y;

  BLOCK_SIZE min_size = x->min_partition_size;
  BLOCK_SIZE max_size = x->max_partition_size;

#if CONFIG_FP_MB_STATS
  unsigned int src_diff_var = UINT_MAX;
  int none_complexity = 0;
#endif

  int partition_none_allowed = !force_horz_split && !force_vert_split;
  int partition_horz_allowed = !force_vert_split && yss <= xss &&
                               bsize >= BLOCK_8X8;
  int partition_vert_allowed = !force_horz_split && xss <= yss &&
                               bsize >= BLOCK_8X8;
  (void) *tp_orig;

  assert(num_8x8_blocks_wide_lookup[bsize] ==
             num_8x8_blocks_high_lookup[bsize]);

  vp9_rd_cost_init(&this_rdc);
  vp9_rd_cost_init(&sum_rdc);
  vp9_rd_cost_reset(&best_rdc);
  best_rdc.rdcost = best_rd;

  set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);

  if (bsize == BLOCK_16X16 && cpi->oxcf.aq_mode)
    x->mb_energy = vp9_block_energy(cpi, x, bsize);

  if (cpi->sf.cb_partition_search && bsize == BLOCK_16X16) {
    int cb_partition_search_ctrl = ((pc_tree->index == 0 || pc_tree->index == 3)
        + get_chessboard_index(cm->current_video_frame)) & 0x1;

    if (cb_partition_search_ctrl && bsize > min_size && bsize < max_size)
      set_partition_range(cm, xd, mi_row, mi_col, bsize, &min_size, &max_size);
  }

  // Determine partition types in search according to the speed features.
  // The threshold set here has to be of square block size.
  if (cpi->sf.auto_min_max_partition_size) {
    partition_none_allowed &= (bsize <= max_size && bsize >= min_size);
    partition_horz_allowed &= ((bsize <= max_size && bsize > min_size) ||
                                force_horz_split);
    partition_vert_allowed &= ((bsize <= max_size && bsize > min_size) ||
                                force_vert_split);
    do_split &= bsize > min_size;
  }
  if (cpi->sf.use_square_partition_only) {
    partition_horz_allowed &= force_horz_split;
    partition_vert_allowed &= force_vert_split;
  }

  save_context(x, mi_row, mi_col, a, l, sa, sl, bsize);

#if CONFIG_FP_MB_STATS
  if (cpi->use_fp_mb_stats) {
    set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);
    src_diff_var = get_sby_perpixel_diff_variance(cpi, &x->plane[0].src,
                                                  mi_row, mi_col, bsize);
  }
#endif

#if CONFIG_FP_MB_STATS
  // Decide whether we shall split directly and skip searching NONE by using
  // the first pass block statistics
  if (cpi->use_fp_mb_stats && bsize >= BLOCK_32X32 && do_split &&
      partition_none_allowed && src_diff_var > 4 &&
      cm->base_qindex < qindex_split_threshold_lookup[bsize]) {
    int mb_row = mi_row >> 1;
    int mb_col = mi_col >> 1;
    int mb_row_end =
        MIN(mb_row + num_16x16_blocks_high_lookup[bsize], cm->mb_rows);
    int mb_col_end =
        MIN(mb_col + num_16x16_blocks_wide_lookup[bsize], cm->mb_cols);
    int r, c;

    // compute a complexity measure, basically measure inconsistency of motion
    // vectors obtained from the first pass in the current block
    for (r = mb_row; r < mb_row_end ; r++) {
      for (c = mb_col; c < mb_col_end; c++) {
        const int mb_index = r * cm->mb_cols + c;

        MOTION_DIRECTION this_mv;
        MOTION_DIRECTION right_mv;
        MOTION_DIRECTION bottom_mv;

        this_mv =
            get_motion_direction_fp(cpi->twopass.this_frame_mb_stats[mb_index]);

        // to its right
        if (c != mb_col_end - 1) {
          right_mv = get_motion_direction_fp(
              cpi->twopass.this_frame_mb_stats[mb_index + 1]);
          none_complexity += get_motion_inconsistency(this_mv, right_mv);
        }

        // to its bottom
        if (r != mb_row_end - 1) {
          bottom_mv = get_motion_direction_fp(
              cpi->twopass.this_frame_mb_stats[mb_index + cm->mb_cols]);
          none_complexity += get_motion_inconsistency(this_mv, bottom_mv);
        }

        // do not count its left and top neighbors to avoid double counting
      }
    }

    if (none_complexity > complexity_16x16_blocks_threshold[bsize]) {
      partition_none_allowed = 0;
    }
  }
#endif

  // PARTITION_NONE
  if (partition_none_allowed) {
    rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col,
                     &this_rdc, bsize, ctx, best_rdc.rdcost);
    if (this_rdc.rate != INT_MAX) {
      if (bsize >= BLOCK_8X8) {
        pl = partition_plane_context(xd, mi_row, mi_col, bsize);
        this_rdc.rate += cpi->partition_cost[pl][PARTITION_NONE];
        this_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                                 this_rdc.rate, this_rdc.dist);
      }

      if (this_rdc.rdcost < best_rdc.rdcost) {
        int64_t dist_breakout_thr = cpi->sf.partition_search_breakout_dist_thr;
        int rate_breakout_thr = cpi->sf.partition_search_breakout_rate_thr;

        best_rdc = this_rdc;
        if (bsize >= BLOCK_8X8)
          pc_tree->partitioning = PARTITION_NONE;

        // Adjust dist breakout threshold according to the partition size.
        dist_breakout_thr >>= 8 - (b_width_log2_lookup[bsize] +
            b_height_log2_lookup[bsize]);

        rate_breakout_thr *= num_pels_log2_lookup[bsize];

        // If all y, u, v transform blocks in this partition are skippable, and
        // the dist & rate are within the thresholds, the partition search is
        // terminated for current branch of the partition search tree.
        // The dist & rate thresholds are set to 0 at speed 0 to disable the
        // early termination at that speed.
        if (!x->e_mbd.lossless &&
            (ctx->skippable && best_rdc.dist < dist_breakout_thr &&
            best_rdc.rate < rate_breakout_thr)) {
          do_split = 0;
          do_rect = 0;
        }

#if CONFIG_FP_MB_STATS
        // Check if every 16x16 first pass block statistics has zero
        // motion and the corresponding first pass residue is small enough.
        // If that is the case, check the difference variance between the
        // current frame and the last frame. If the variance is small enough,
        // stop further splitting in RD optimization
        if (cpi->use_fp_mb_stats && do_split != 0 &&
            cm->base_qindex > qindex_skip_threshold_lookup[bsize]) {
          int mb_row = mi_row >> 1;
          int mb_col = mi_col >> 1;
          int mb_row_end =
              MIN(mb_row + num_16x16_blocks_high_lookup[bsize], cm->mb_rows);
          int mb_col_end =
              MIN(mb_col + num_16x16_blocks_wide_lookup[bsize], cm->mb_cols);
          int r, c;

          int skip = 1;
          for (r = mb_row; r < mb_row_end; r++) {
            for (c = mb_col; c < mb_col_end; c++) {
              const int mb_index = r * cm->mb_cols + c;
              if (!(cpi->twopass.this_frame_mb_stats[mb_index] &
                    FPMB_MOTION_ZERO_MASK) ||
                  !(cpi->twopass.this_frame_mb_stats[mb_index] &
                    FPMB_ERROR_SMALL_MASK)) {
                skip = 0;
                break;
              }
            }
            if (skip == 0) {
              break;
            }
          }
          if (skip) {
            if (src_diff_var == UINT_MAX) {
              set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);
              src_diff_var = get_sby_perpixel_diff_variance(
                  cpi, &x->plane[0].src, mi_row, mi_col, bsize);
            }
            if (src_diff_var < 8) {
              do_split = 0;
              do_rect = 0;
            }
          }
        }
#endif
      }
    }
    restore_context(x, mi_row, mi_col, a, l, sa, sl, bsize);
  }

  // store estimated motion vector
  if (cpi->sf.adaptive_motion_search)
    store_pred_mv(x, ctx);

  // PARTITION_SPLIT
  // TODO(jingning): use the motion vectors given by the above search as
  // the starting point of motion search in the following partition type check.
  if (do_split) {
    subsize = get_subsize(bsize, PARTITION_SPLIT);
    if (bsize == BLOCK_8X8) {
      i = 4;
      if (cpi->sf.adaptive_pred_interp_filter && partition_none_allowed)
        pc_tree->leaf_split[0]->pred_interp_filter =
            ctx->mic.mbmi.interp_filter;
      rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &sum_rdc, subsize,
                       pc_tree->leaf_split[0], best_rdc.rdcost);
      if (sum_rdc.rate == INT_MAX)
        sum_rdc.rdcost = INT64_MAX;
    } else {
      for (i = 0; i < 4 && sum_rdc.rdcost < best_rdc.rdcost; ++i) {
      const int x_idx = (i & 1) * mi_step;
      const int y_idx = (i >> 1) * mi_step;

        if (mi_row + y_idx >= cm->mi_rows || mi_col + x_idx >= cm->mi_cols)
          continue;

        if (cpi->sf.adaptive_motion_search)
          load_pred_mv(x, ctx);

        pc_tree->split[i]->index = i;
        rd_pick_partition(cpi, td, tile_data, tp,
                          mi_row + y_idx, mi_col + x_idx,
                          subsize, &this_rdc,
                          best_rdc.rdcost - sum_rdc.rdcost, pc_tree->split[i]);

        if (this_rdc.rate == INT_MAX) {
          sum_rdc.rdcost = INT64_MAX;
          break;
        } else {
          sum_rdc.rate += this_rdc.rate;
          sum_rdc.dist += this_rdc.dist;
          sum_rdc.rdcost += this_rdc.rdcost;
        }
      }
    }

    if (sum_rdc.rdcost < best_rdc.rdcost && i == 4) {
      pl = partition_plane_context(xd, mi_row, mi_col, bsize);
      sum_rdc.rate += cpi->partition_cost[pl][PARTITION_SPLIT];
      sum_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                              sum_rdc.rate, sum_rdc.dist);

      if (sum_rdc.rdcost < best_rdc.rdcost) {
        best_rdc = sum_rdc;
        pc_tree->partitioning = PARTITION_SPLIT;
      }
    } else {
      // skip rectangular partition test when larger block size
      // gives better rd cost
      if (cpi->sf.less_rectangular_check)
        do_rect &= !partition_none_allowed;
    }
    restore_context(x, mi_row, mi_col, a, l, sa, sl, bsize);
  }

  // PARTITION_HORZ
  if (partition_horz_allowed &&
      (do_rect || vp9_active_h_edge(cpi, mi_row, mi_step))) {
      subsize = get_subsize(bsize, PARTITION_HORZ);
    if (cpi->sf.adaptive_motion_search)
      load_pred_mv(x, ctx);
    if (cpi->sf.adaptive_pred_interp_filter && bsize == BLOCK_8X8 &&
        partition_none_allowed)
      pc_tree->horizontal[0].pred_interp_filter =
          ctx->mic.mbmi.interp_filter;
    rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &sum_rdc, subsize,
                     &pc_tree->horizontal[0], best_rdc.rdcost);

    if (sum_rdc.rdcost < best_rdc.rdcost && mi_row + mi_step < cm->mi_rows &&
        bsize > BLOCK_8X8) {
      PICK_MODE_CONTEXT *ctx = &pc_tree->horizontal[0];
      update_state(cpi, td, ctx, mi_row, mi_col, subsize, 0);
      encode_superblock(cpi, td, tp, 0, mi_row, mi_col, subsize, ctx);

      if (cpi->sf.adaptive_motion_search)
        load_pred_mv(x, ctx);
      if (cpi->sf.adaptive_pred_interp_filter && bsize == BLOCK_8X8 &&
          partition_none_allowed)
        pc_tree->horizontal[1].pred_interp_filter =
            ctx->mic.mbmi.interp_filter;
      rd_pick_sb_modes(cpi, tile_data, x, mi_row + mi_step, mi_col,
                       &this_rdc, subsize, &pc_tree->horizontal[1],
                       best_rdc.rdcost - sum_rdc.rdcost);
      if (this_rdc.rate == INT_MAX) {
        sum_rdc.rdcost = INT64_MAX;
      } else {
        sum_rdc.rate += this_rdc.rate;
        sum_rdc.dist += this_rdc.dist;
        sum_rdc.rdcost += this_rdc.rdcost;
      }
    }

    if (sum_rdc.rdcost < best_rdc.rdcost) {
      pl = partition_plane_context(xd, mi_row, mi_col, bsize);
      sum_rdc.rate += cpi->partition_cost[pl][PARTITION_HORZ];
      sum_rdc.rdcost = RDCOST(x->rdmult, x->rddiv, sum_rdc.rate, sum_rdc.dist);
      if (sum_rdc.rdcost < best_rdc.rdcost) {
        best_rdc = sum_rdc;
        pc_tree->partitioning = PARTITION_HORZ;
      }
    }
    restore_context(x, mi_row, mi_col, a, l, sa, sl, bsize);
  }
  // PARTITION_VERT
  if (partition_vert_allowed &&
      (do_rect || vp9_active_v_edge(cpi, mi_col, mi_step))) {
      subsize = get_subsize(bsize, PARTITION_VERT);

    if (cpi->sf.adaptive_motion_search)
      load_pred_mv(x, ctx);
    if (cpi->sf.adaptive_pred_interp_filter && bsize == BLOCK_8X8 &&
        partition_none_allowed)
      pc_tree->vertical[0].pred_interp_filter =
          ctx->mic.mbmi.interp_filter;
    rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &sum_rdc, subsize,
                     &pc_tree->vertical[0], best_rdc.rdcost);
    if (sum_rdc.rdcost < best_rdc.rdcost && mi_col + mi_step < cm->mi_cols &&
        bsize > BLOCK_8X8) {
      update_state(cpi, td, &pc_tree->vertical[0], mi_row, mi_col, subsize, 0);
      encode_superblock(cpi, td, tp, 0, mi_row, mi_col, subsize,
                        &pc_tree->vertical[0]);

      if (cpi->sf.adaptive_motion_search)
        load_pred_mv(x, ctx);
      if (cpi->sf.adaptive_pred_interp_filter && bsize == BLOCK_8X8 &&
          partition_none_allowed)
        pc_tree->vertical[1].pred_interp_filter =
            ctx->mic.mbmi.interp_filter;
      rd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col + mi_step,
                       &this_rdc, subsize,
                       &pc_tree->vertical[1], best_rdc.rdcost - sum_rdc.rdcost);
      if (this_rdc.rate == INT_MAX) {
        sum_rdc.rdcost = INT64_MAX;
      } else {
        sum_rdc.rate += this_rdc.rate;
        sum_rdc.dist += this_rdc.dist;
        sum_rdc.rdcost += this_rdc.rdcost;
      }
    }

    if (sum_rdc.rdcost < best_rdc.rdcost) {
      pl = partition_plane_context(xd, mi_row, mi_col, bsize);
      sum_rdc.rate += cpi->partition_cost[pl][PARTITION_VERT];
      sum_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                              sum_rdc.rate, sum_rdc.dist);
      if (sum_rdc.rdcost < best_rdc.rdcost) {
        best_rdc = sum_rdc;
        pc_tree->partitioning = PARTITION_VERT;
      }
    }
    restore_context(x, mi_row, mi_col, a, l, sa, sl, bsize);
  }

  // TODO(jbb): This code added so that we avoid static analysis
  // warning related to the fact that best_rd isn't used after this
  // point.  This code should be refactored so that the duplicate
  // checks occur in some sub function and thus are used...
  (void) best_rd;
  *rd_cost = best_rdc;


  if (best_rdc.rate < INT_MAX && best_rdc.dist < INT64_MAX &&
      pc_tree->index != 3) {
    int output_enabled = (bsize == BLOCK_64X64);
    encode_sb(cpi, td, tile_info, tp, mi_row, mi_col, output_enabled,
              bsize, pc_tree);
  }

  if (bsize == BLOCK_64X64) {
    assert(tp_orig < *tp);
    assert(best_rdc.rate < INT_MAX);
    assert(best_rdc.dist < INT64_MAX);
  } else {
    assert(tp_orig == *tp);
  }
}

static void encode_rd_sb_row(VP9_COMP *cpi,
                             ThreadData *td,
                             TileDataEnc *tile_data,
                             int mi_row,
                             TOKENEXTRA **tp) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  SPEED_FEATURES *const sf = &cpi->sf;
  int mi_col;

  // Initialize the left context for the new SB row
  memset(&xd->left_context, 0, sizeof(xd->left_context));
  memset(xd->left_seg_context, 0, sizeof(xd->left_seg_context));

  // Code each SB in the row
  for (mi_col = tile_info->mi_col_start; mi_col < tile_info->mi_col_end;
       mi_col += MI_BLOCK_SIZE) {
    const struct segmentation *const seg = &cm->seg;
    int dummy_rate;
    int64_t dummy_dist;
    RD_COST dummy_rdc;
    int i;
    int seg_skip = 0;

    const int idx_str = cm->mi_stride * mi_row + mi_col;
    MODE_INFO **mi = cm->mi_grid_visible + idx_str;

    if (sf->adaptive_pred_interp_filter) {
      for (i = 0; i < 64; ++i)
        td->leaf_tree[i].pred_interp_filter = SWITCHABLE;

      for (i = 0; i < 64; ++i) {
        td->pc_tree[i].vertical[0].pred_interp_filter = SWITCHABLE;
        td->pc_tree[i].vertical[1].pred_interp_filter = SWITCHABLE;
        td->pc_tree[i].horizontal[0].pred_interp_filter = SWITCHABLE;
        td->pc_tree[i].horizontal[1].pred_interp_filter = SWITCHABLE;
      }
    }

    vp9_zero(x->pred_mv);
    td->pc_root->index = 0;

    if (seg->enabled) {
      const uint8_t *const map = seg->update_map ? cpi->segmentation_map
                                                 : cm->last_frame_seg_map;
      int segment_id = get_segment_id(cm, map, BLOCK_64X64, mi_row, mi_col);
      seg_skip = segfeature_active(seg, segment_id, SEG_LVL_SKIP);
    }

    x->source_variance = UINT_MAX;
    if (sf->partition_search_type == FIXED_PARTITION || seg_skip) {
      const BLOCK_SIZE bsize =
          seg_skip ? BLOCK_64X64 : sf->always_this_block_size;
      set_offsets(cpi, tile_info, x, mi_row, mi_col, BLOCK_64X64);
      set_fixed_partitioning(cpi, tile_info, mi, mi_row, mi_col, bsize);
      rd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                       BLOCK_64X64, &dummy_rate, &dummy_dist, 1, td->pc_root);
    } else if (cpi->partition_search_skippable_frame) {
      BLOCK_SIZE bsize;
      set_offsets(cpi, tile_info, x, mi_row, mi_col, BLOCK_64X64);
      bsize = get_rd_var_based_fixed_partition(cpi, x, mi_row, mi_col);
      set_fixed_partitioning(cpi, tile_info, mi, mi_row, mi_col, bsize);
      rd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                       BLOCK_64X64, &dummy_rate, &dummy_dist, 1, td->pc_root);
    } else if (sf->partition_search_type == VAR_BASED_PARTITION &&
               cm->frame_type != KEY_FRAME) {
      choose_partitioning(cpi, tile_info, x, mi_row, mi_col);
      rd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                       BLOCK_64X64, &dummy_rate, &dummy_dist, 1, td->pc_root);
    } else {
      // If required set upper and lower partition size limits
      if (sf->auto_min_max_partition_size) {
        set_offsets(cpi, tile_info, x, mi_row, mi_col, BLOCK_64X64);
        rd_auto_partition_range(cpi, tile_info, xd, mi_row, mi_col,
                                &x->min_partition_size,
                                &x->max_partition_size);
      }
      rd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, BLOCK_64X64,
                        &dummy_rdc, INT64_MAX, td->pc_root);
    }
  }
}

static void init_encode_frame_mb_context(VP9_COMP *cpi) {
  MACROBLOCK *const x = &cpi->td.mb;
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int aligned_mi_cols = mi_cols_aligned_to_sb(cm->mi_cols);

  // Copy data over into macro block data structures.
  vp9_setup_src_planes(x, cpi->Source, 0, 0);

  vp9_setup_block_planes(&x->e_mbd, cm->subsampling_x, cm->subsampling_y);

  // Note: this memset assumes above_context[0], [1] and [2]
  // are allocated as part of the same buffer.
  memset(xd->above_context[0], 0,
         sizeof(*xd->above_context[0]) *
         2 * aligned_mi_cols * MAX_MB_PLANE);
  memset(xd->above_seg_context, 0,
         sizeof(*xd->above_seg_context) * aligned_mi_cols);
}

static int check_dual_ref_flags(VP9_COMP *cpi) {
  const int ref_flags = cpi->ref_frame_flags;

  if (segfeature_active(&cpi->common.seg, 1, SEG_LVL_REF_FRAME)) {
    return 0;
  } else {
    return (!!(ref_flags & VP9_GOLD_FLAG) + !!(ref_flags & VP9_LAST_FLAG)
        + !!(ref_flags & VP9_ALT_FLAG)) >= 2;
  }
}

static void reset_skip_tx_size(VP9_COMMON *cm, TX_SIZE max_tx_size) {
  int mi_row, mi_col;
  const int mis = cm->mi_stride;
  MODE_INFO **mi_ptr = cm->mi_grid_visible;

  for (mi_row = 0; mi_row < cm->mi_rows; ++mi_row, mi_ptr += mis) {
    for (mi_col = 0; mi_col < cm->mi_cols; ++mi_col) {
      if (mi_ptr[mi_col]->mbmi.tx_size > max_tx_size)
        mi_ptr[mi_col]->mbmi.tx_size = max_tx_size;
    }
  }
}

static MV_REFERENCE_FRAME get_frame_type(const VP9_COMP *cpi) {
  if (frame_is_intra_only(&cpi->common))
    return INTRA_FRAME;
  else if (cpi->rc.is_src_frame_alt_ref && cpi->refresh_golden_frame)
    return ALTREF_FRAME;
  else if (cpi->refresh_golden_frame || cpi->refresh_alt_ref_frame)
    return GOLDEN_FRAME;
  else
    return LAST_FRAME;
}

static TX_MODE select_tx_mode(const VP9_COMP *cpi, MACROBLOCKD *const xd) {
  if (xd->lossless)
    return ONLY_4X4;
  if (cpi->common.frame_type == KEY_FRAME &&
      cpi->sf.use_nonrd_pick_mode)
    return ALLOW_16X16;
  if (cpi->sf.tx_size_search_method == USE_LARGESTALL)
    return ALLOW_32X32;
  else if (cpi->sf.tx_size_search_method == USE_FULL_RD||
           cpi->sf.tx_size_search_method == USE_TX_8X8)
    return TX_MODE_SELECT;
  else
    return cpi->common.tx_mode;
}

static void hybrid_intra_mode_search(VP9_COMP *cpi, MACROBLOCK *const x,
                                     RD_COST *rd_cost, BLOCK_SIZE bsize,
                                     PICK_MODE_CONTEXT *ctx) {
  if (bsize < BLOCK_16X16)
    vp9_rd_pick_intra_mode_sb(cpi, x, rd_cost, bsize, ctx, INT64_MAX);
  else
    vp9_pick_intra_mode(cpi, x, rd_cost, bsize, ctx);
}

static void nonrd_pick_sb_modes(VP9_COMP *cpi,
                                TileDataEnc *tile_data, MACROBLOCK *const x,
                                int mi_row, int mi_col, RD_COST *rd_cost,
                                BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *mbmi;
  set_offsets(cpi, tile_info, x, mi_row, mi_col, bsize);
  mbmi = &xd->mi[0]->mbmi;
  mbmi->sb_type = bsize;

  if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ && cm->seg.enabled)
    if (cyclic_refresh_segment_id_boosted(mbmi->segment_id))
      x->rdmult = vp9_cyclic_refresh_get_rdmult(cpi->cyclic_refresh);

  if (cm->frame_type == KEY_FRAME)
    hybrid_intra_mode_search(cpi, x, rd_cost, bsize, ctx);
  else if (segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP))
    set_mode_info_seg_skip(x, cm->tx_mode, rd_cost, bsize);
  else if (bsize >= BLOCK_8X8)
    vp9_pick_inter_mode(cpi, x, tile_data, mi_row, mi_col,
                        rd_cost, bsize, ctx);
  else
    vp9_pick_inter_mode_sub8x8(cpi, x, mi_row, mi_col,
                               rd_cost, bsize, ctx);

  duplicate_mode_info_in_sb(cm, xd, mi_row, mi_col, bsize);

  if (rd_cost->rate == INT_MAX)
    vp9_rd_cost_reset(rd_cost);

  ctx->rate = rd_cost->rate;
  ctx->dist = rd_cost->dist;
}

static void fill_mode_info_sb(VP9_COMMON *cm, MACROBLOCK *x,
                              int mi_row, int mi_col,
                              BLOCK_SIZE bsize,
                              PC_TREE *pc_tree) {
  MACROBLOCKD *xd = &x->e_mbd;
  int bsl = b_width_log2_lookup[bsize], hbs = (1 << bsl) / 4;
  PARTITION_TYPE partition = pc_tree->partitioning;
  BLOCK_SIZE subsize = get_subsize(bsize, partition);

  assert(bsize >= BLOCK_8X8);

  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  switch (partition) {
    case PARTITION_NONE:
      set_mode_info_offsets(cm, x, xd, mi_row, mi_col);
      *(xd->mi[0]) = pc_tree->none.mic;
      *(x->mbmi_ext) = pc_tree->none.mbmi_ext;
      duplicate_mode_info_in_sb(cm, xd, mi_row, mi_col, bsize);
      break;
    case PARTITION_VERT:
      set_mode_info_offsets(cm, x, xd, mi_row, mi_col);
      *(xd->mi[0]) = pc_tree->vertical[0].mic;
      *(x->mbmi_ext) = pc_tree->vertical[0].mbmi_ext;
      duplicate_mode_info_in_sb(cm, xd, mi_row, mi_col, subsize);

      if (mi_col + hbs < cm->mi_cols) {
        set_mode_info_offsets(cm, x, xd, mi_row, mi_col + hbs);
        *(xd->mi[0]) = pc_tree->vertical[1].mic;
        *(x->mbmi_ext) = pc_tree->vertical[1].mbmi_ext;
        duplicate_mode_info_in_sb(cm, xd, mi_row, mi_col + hbs, subsize);
      }
      break;
    case PARTITION_HORZ:
      set_mode_info_offsets(cm, x, xd, mi_row, mi_col);
      *(xd->mi[0]) = pc_tree->horizontal[0].mic;
      *(x->mbmi_ext) = pc_tree->horizontal[0].mbmi_ext;
      duplicate_mode_info_in_sb(cm, xd, mi_row, mi_col, subsize);
      if (mi_row + hbs < cm->mi_rows) {
        set_mode_info_offsets(cm, x, xd, mi_row + hbs, mi_col);
        *(xd->mi[0]) = pc_tree->horizontal[1].mic;
        *(x->mbmi_ext) = pc_tree->horizontal[1].mbmi_ext;
        duplicate_mode_info_in_sb(cm, xd, mi_row + hbs, mi_col, subsize);
      }
      break;
    case PARTITION_SPLIT: {
      fill_mode_info_sb(cm, x, mi_row, mi_col, subsize, pc_tree->split[0]);
      fill_mode_info_sb(cm, x, mi_row, mi_col + hbs, subsize,
                        pc_tree->split[1]);
      fill_mode_info_sb(cm, x, mi_row + hbs, mi_col, subsize,
                        pc_tree->split[2]);
      fill_mode_info_sb(cm, x, mi_row + hbs, mi_col + hbs, subsize,
                        pc_tree->split[3]);
      break;
    }
    default:
      break;
  }
}

// Reset the prediction pixel ready flag recursively.
static void pred_pixel_ready_reset(PC_TREE *pc_tree, BLOCK_SIZE bsize) {
  pc_tree->none.pred_pixel_ready = 0;
  pc_tree->horizontal[0].pred_pixel_ready = 0;
  pc_tree->horizontal[1].pred_pixel_ready = 0;
  pc_tree->vertical[0].pred_pixel_ready = 0;
  pc_tree->vertical[1].pred_pixel_ready = 0;

  if (bsize > BLOCK_8X8) {
    BLOCK_SIZE subsize = get_subsize(bsize, PARTITION_SPLIT);
    int i;
    for (i = 0; i < 4; ++i)
      pred_pixel_ready_reset(pc_tree->split[i], subsize);
  }
}

static void nonrd_pick_partition(VP9_COMP *cpi, ThreadData *td,
                                 TileDataEnc *tile_data,
                                 TOKENEXTRA **tp, int mi_row,
                                 int mi_col, BLOCK_SIZE bsize, RD_COST *rd_cost,
                                 int do_recon, int64_t best_rd,
                                 PC_TREE *pc_tree) {
  const SPEED_FEATURES *const sf = &cpi->sf;
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int ms = num_8x8_blocks_wide_lookup[bsize] / 2;
  TOKENEXTRA *tp_orig = *tp;
  PICK_MODE_CONTEXT *ctx = &pc_tree->none;
  int i;
  BLOCK_SIZE subsize = bsize;
  RD_COST this_rdc, sum_rdc, best_rdc;
  int do_split = bsize >= BLOCK_8X8;
  int do_rect = 1;
  // Override skipping rectangular partition operations for edge blocks
  const int force_horz_split = (mi_row + ms >= cm->mi_rows);
  const int force_vert_split = (mi_col + ms >= cm->mi_cols);
  const int xss = x->e_mbd.plane[1].subsampling_x;
  const int yss = x->e_mbd.plane[1].subsampling_y;

  int partition_none_allowed = !force_horz_split && !force_vert_split;
  int partition_horz_allowed = !force_vert_split && yss <= xss &&
                               bsize >= BLOCK_8X8;
  int partition_vert_allowed = !force_horz_split && xss <= yss &&
                               bsize >= BLOCK_8X8;
  (void) *tp_orig;

  assert(num_8x8_blocks_wide_lookup[bsize] ==
             num_8x8_blocks_high_lookup[bsize]);

  vp9_rd_cost_init(&sum_rdc);
  vp9_rd_cost_reset(&best_rdc);
  best_rdc.rdcost = best_rd;

  // Determine partition types in search according to the speed features.
  // The threshold set here has to be of square block size.
  if (sf->auto_min_max_partition_size) {
    partition_none_allowed &= (bsize <= x->max_partition_size &&
                               bsize >= x->min_partition_size);
    partition_horz_allowed &= ((bsize <= x->max_partition_size &&
                                bsize > x->min_partition_size) ||
                                force_horz_split);
    partition_vert_allowed &= ((bsize <= x->max_partition_size &&
                                bsize > x->min_partition_size) ||
                                force_vert_split);
    do_split &= bsize > x->min_partition_size;
  }
  if (sf->use_square_partition_only) {
    partition_horz_allowed &= force_horz_split;
    partition_vert_allowed &= force_vert_split;
  }

  ctx->pred_pixel_ready = !(partition_vert_allowed ||
                            partition_horz_allowed ||
                            do_split);

  // PARTITION_NONE
  if (partition_none_allowed) {
    nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col,
                        &this_rdc, bsize, ctx);
    ctx->mic.mbmi = xd->mi[0]->mbmi;
    ctx->mbmi_ext = *x->mbmi_ext;
    ctx->skip_txfm[0] = x->skip_txfm[0];
    ctx->skip = x->skip;

    if (this_rdc.rate != INT_MAX) {
      int pl = partition_plane_context(xd, mi_row, mi_col, bsize);
      this_rdc.rate += cpi->partition_cost[pl][PARTITION_NONE];
      this_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                              this_rdc.rate, this_rdc.dist);
      if (this_rdc.rdcost < best_rdc.rdcost) {
        int64_t dist_breakout_thr = sf->partition_search_breakout_dist_thr;
        int64_t rate_breakout_thr = sf->partition_search_breakout_rate_thr;

        dist_breakout_thr >>= 8 - (b_width_log2_lookup[bsize] +
            b_height_log2_lookup[bsize]);

        rate_breakout_thr *= num_pels_log2_lookup[bsize];

        best_rdc = this_rdc;
        if (bsize >= BLOCK_8X8)
          pc_tree->partitioning = PARTITION_NONE;

        if (!x->e_mbd.lossless &&
            this_rdc.rate < rate_breakout_thr &&
            this_rdc.dist < dist_breakout_thr) {
          do_split = 0;
          do_rect = 0;
        }
      }
    }
  }

  // store estimated motion vector
  store_pred_mv(x, ctx);

  // PARTITION_SPLIT
  if (do_split) {
    int pl = partition_plane_context(xd, mi_row, mi_col, bsize);
    sum_rdc.rate += cpi->partition_cost[pl][PARTITION_SPLIT];
    sum_rdc.rdcost = RDCOST(x->rdmult, x->rddiv, sum_rdc.rate, sum_rdc.dist);
    subsize = get_subsize(bsize, PARTITION_SPLIT);
    for (i = 0; i < 4 && sum_rdc.rdcost < best_rdc.rdcost; ++i) {
      const int x_idx = (i & 1) * ms;
      const int y_idx = (i >> 1) * ms;

      if (mi_row + y_idx >= cm->mi_rows || mi_col + x_idx >= cm->mi_cols)
        continue;
      load_pred_mv(x, ctx);
      nonrd_pick_partition(cpi, td, tile_data, tp,
                           mi_row + y_idx, mi_col + x_idx,
                           subsize, &this_rdc, 0,
                           best_rdc.rdcost - sum_rdc.rdcost, pc_tree->split[i]);

      if (this_rdc.rate == INT_MAX) {
        vp9_rd_cost_reset(&sum_rdc);
      } else {
        sum_rdc.rate += this_rdc.rate;
        sum_rdc.dist += this_rdc.dist;
        sum_rdc.rdcost += this_rdc.rdcost;
      }
    }

    if (sum_rdc.rdcost < best_rdc.rdcost) {
      best_rdc = sum_rdc;
      pc_tree->partitioning = PARTITION_SPLIT;
    } else {
      // skip rectangular partition test when larger block size
      // gives better rd cost
      if (sf->less_rectangular_check)
        do_rect &= !partition_none_allowed;
    }
  }

  // PARTITION_HORZ
  if (partition_horz_allowed && do_rect) {
    subsize = get_subsize(bsize, PARTITION_HORZ);
    if (sf->adaptive_motion_search)
      load_pred_mv(x, ctx);
    pc_tree->horizontal[0].pred_pixel_ready = 1;
    nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &sum_rdc, subsize,
                        &pc_tree->horizontal[0]);

    pc_tree->horizontal[0].mic.mbmi = xd->mi[0]->mbmi;
    pc_tree->horizontal[0].mbmi_ext = *x->mbmi_ext;
    pc_tree->horizontal[0].skip_txfm[0] = x->skip_txfm[0];
    pc_tree->horizontal[0].skip = x->skip;

    if (sum_rdc.rdcost < best_rdc.rdcost && mi_row + ms < cm->mi_rows) {
      load_pred_mv(x, ctx);
      pc_tree->horizontal[1].pred_pixel_ready = 1;
      nonrd_pick_sb_modes(cpi, tile_data, x, mi_row + ms, mi_col,
                          &this_rdc, subsize,
                          &pc_tree->horizontal[1]);

      pc_tree->horizontal[1].mic.mbmi = xd->mi[0]->mbmi;
      pc_tree->horizontal[1].mbmi_ext = *x->mbmi_ext;
      pc_tree->horizontal[1].skip_txfm[0] = x->skip_txfm[0];
      pc_tree->horizontal[1].skip = x->skip;

      if (this_rdc.rate == INT_MAX) {
        vp9_rd_cost_reset(&sum_rdc);
      } else {
        int pl = partition_plane_context(xd, mi_row, mi_col, bsize);
        this_rdc.rate += cpi->partition_cost[pl][PARTITION_HORZ];
        sum_rdc.rate += this_rdc.rate;
        sum_rdc.dist += this_rdc.dist;
        sum_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                                sum_rdc.rate, sum_rdc.dist);
      }
    }

    if (sum_rdc.rdcost < best_rdc.rdcost) {
      best_rdc = sum_rdc;
      pc_tree->partitioning = PARTITION_HORZ;
    } else {
      pred_pixel_ready_reset(pc_tree, bsize);
    }
  }

  // PARTITION_VERT
  if (partition_vert_allowed && do_rect) {
    subsize = get_subsize(bsize, PARTITION_VERT);
    if (sf->adaptive_motion_search)
      load_pred_mv(x, ctx);
    pc_tree->vertical[0].pred_pixel_ready = 1;
    nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, &sum_rdc, subsize,
                        &pc_tree->vertical[0]);
    pc_tree->vertical[0].mic.mbmi = xd->mi[0]->mbmi;
    pc_tree->vertical[0].mbmi_ext = *x->mbmi_ext;
    pc_tree->vertical[0].skip_txfm[0] = x->skip_txfm[0];
    pc_tree->vertical[0].skip = x->skip;

    if (sum_rdc.rdcost < best_rdc.rdcost && mi_col + ms < cm->mi_cols) {
      load_pred_mv(x, ctx);
      pc_tree->vertical[1].pred_pixel_ready = 1;
      nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col + ms,
                          &this_rdc, subsize,
                          &pc_tree->vertical[1]);
      pc_tree->vertical[1].mic.mbmi = xd->mi[0]->mbmi;
      pc_tree->vertical[1].mbmi_ext = *x->mbmi_ext;
      pc_tree->vertical[1].skip_txfm[0] = x->skip_txfm[0];
      pc_tree->vertical[1].skip = x->skip;

      if (this_rdc.rate == INT_MAX) {
        vp9_rd_cost_reset(&sum_rdc);
      } else {
        int pl = partition_plane_context(xd, mi_row, mi_col, bsize);
        sum_rdc.rate += cpi->partition_cost[pl][PARTITION_VERT];
        sum_rdc.rate += this_rdc.rate;
        sum_rdc.dist += this_rdc.dist;
        sum_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                                sum_rdc.rate, sum_rdc.dist);
      }
    }

    if (sum_rdc.rdcost < best_rdc.rdcost) {
      best_rdc = sum_rdc;
      pc_tree->partitioning = PARTITION_VERT;
    } else {
      pred_pixel_ready_reset(pc_tree, bsize);
    }
  }

  *rd_cost = best_rdc;

  if (best_rdc.rate == INT_MAX) {
    vp9_rd_cost_reset(rd_cost);
    return;
  }

  // update mode info array
  fill_mode_info_sb(cm, x, mi_row, mi_col, bsize, pc_tree);

  if (best_rdc.rate < INT_MAX && best_rdc.dist < INT64_MAX && do_recon) {
    int output_enabled = (bsize == BLOCK_64X64);
    encode_sb_rt(cpi, td, tile_info, tp, mi_row, mi_col, output_enabled,
                 bsize, pc_tree);
  }

  if (bsize == BLOCK_64X64 && do_recon) {
    assert(tp_orig < *tp);
    assert(best_rdc.rate < INT_MAX);
    assert(best_rdc.dist < INT64_MAX);
  } else {
    assert(tp_orig == *tp);
  }
}

static void nonrd_select_partition(VP9_COMP *cpi,
                                   ThreadData *td,
                                   TileDataEnc *tile_data,
                                   MODE_INFO **mi,
                                   TOKENEXTRA **tp,
                                   int mi_row, int mi_col,
                                   BLOCK_SIZE bsize, int output_enabled,
                                   RD_COST *rd_cost, PC_TREE *pc_tree) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int bsl = b_width_log2_lookup[bsize], hbs = (1 << bsl) / 4;
  const int mis = cm->mi_stride;
  PARTITION_TYPE partition;
  BLOCK_SIZE subsize;
  RD_COST this_rdc;

  vp9_rd_cost_reset(&this_rdc);
  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  subsize = (bsize >= BLOCK_8X8) ? mi[0]->mbmi.sb_type : BLOCK_4X4;
  partition = partition_lookup[bsl][subsize];

  if (bsize == BLOCK_32X32 && subsize == BLOCK_32X32) {
    x->max_partition_size = BLOCK_32X32;
    x->min_partition_size = BLOCK_16X16;
    nonrd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, bsize,
                         rd_cost, 0, INT64_MAX, pc_tree);
  } else if (bsize == BLOCK_32X32 && partition != PARTITION_NONE &&
             subsize >= BLOCK_16X16) {
    x->max_partition_size = BLOCK_32X32;
    x->min_partition_size = BLOCK_8X8;
    nonrd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, bsize,
                         rd_cost, 0, INT64_MAX, pc_tree);
  } else if (bsize == BLOCK_16X16 && partition != PARTITION_NONE) {
    x->max_partition_size = BLOCK_16X16;
    x->min_partition_size = BLOCK_8X8;
    nonrd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col, bsize,
                         rd_cost, 0, INT64_MAX, pc_tree);
  } else {
    switch (partition) {
      case PARTITION_NONE:
        pc_tree->none.pred_pixel_ready = 1;
        nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, rd_cost,
                            subsize, &pc_tree->none);
        pc_tree->none.mic.mbmi = xd->mi[0]->mbmi;
        pc_tree->none.mbmi_ext = *x->mbmi_ext;
        pc_tree->none.skip_txfm[0] = x->skip_txfm[0];
        pc_tree->none.skip = x->skip;
        break;
      case PARTITION_VERT:
        pc_tree->vertical[0].pred_pixel_ready = 1;
        nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, rd_cost,
                            subsize, &pc_tree->vertical[0]);
        pc_tree->vertical[0].mic.mbmi = xd->mi[0]->mbmi;
        pc_tree->vertical[0].mbmi_ext = *x->mbmi_ext;
        pc_tree->vertical[0].skip_txfm[0] = x->skip_txfm[0];
        pc_tree->vertical[0].skip = x->skip;
        if (mi_col + hbs < cm->mi_cols) {
          pc_tree->vertical[1].pred_pixel_ready = 1;
          nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col + hbs,
                              &this_rdc, subsize, &pc_tree->vertical[1]);
          pc_tree->vertical[1].mic.mbmi = xd->mi[0]->mbmi;
          pc_tree->vertical[1].mbmi_ext = *x->mbmi_ext;
          pc_tree->vertical[1].skip_txfm[0] = x->skip_txfm[0];
          pc_tree->vertical[1].skip = x->skip;
          if (this_rdc.rate != INT_MAX && this_rdc.dist != INT64_MAX &&
              rd_cost->rate != INT_MAX && rd_cost->dist != INT64_MAX) {
            rd_cost->rate += this_rdc.rate;
            rd_cost->dist += this_rdc.dist;
          }
        }
        break;
      case PARTITION_HORZ:
        pc_tree->horizontal[0].pred_pixel_ready = 1;
        nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, rd_cost,
                            subsize, &pc_tree->horizontal[0]);
        pc_tree->horizontal[0].mic.mbmi = xd->mi[0]->mbmi;
        pc_tree->horizontal[0].mbmi_ext = *x->mbmi_ext;
        pc_tree->horizontal[0].skip_txfm[0] = x->skip_txfm[0];
        pc_tree->horizontal[0].skip = x->skip;
        if (mi_row + hbs < cm->mi_rows) {
          pc_tree->horizontal[1].pred_pixel_ready = 1;
          nonrd_pick_sb_modes(cpi, tile_data, x, mi_row + hbs, mi_col,
                              &this_rdc, subsize, &pc_tree->horizontal[1]);
          pc_tree->horizontal[1].mic.mbmi = xd->mi[0]->mbmi;
          pc_tree->horizontal[1].mbmi_ext = *x->mbmi_ext;
          pc_tree->horizontal[1].skip_txfm[0] = x->skip_txfm[0];
          pc_tree->horizontal[1].skip = x->skip;
          if (this_rdc.rate != INT_MAX && this_rdc.dist != INT64_MAX &&
              rd_cost->rate != INT_MAX && rd_cost->dist != INT64_MAX) {
            rd_cost->rate += this_rdc.rate;
            rd_cost->dist += this_rdc.dist;
          }
        }
        break;
      case PARTITION_SPLIT:
        subsize = get_subsize(bsize, PARTITION_SPLIT);
        nonrd_select_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                               subsize, output_enabled, rd_cost,
                               pc_tree->split[0]);
        nonrd_select_partition(cpi, td, tile_data, mi + hbs, tp,
                               mi_row, mi_col + hbs, subsize, output_enabled,
                               &this_rdc, pc_tree->split[1]);
        if (this_rdc.rate != INT_MAX && this_rdc.dist != INT64_MAX &&
            rd_cost->rate != INT_MAX && rd_cost->dist != INT64_MAX) {
          rd_cost->rate += this_rdc.rate;
          rd_cost->dist += this_rdc.dist;
        }
        nonrd_select_partition(cpi, td, tile_data, mi + hbs * mis, tp,
                               mi_row + hbs, mi_col, subsize, output_enabled,
                               &this_rdc, pc_tree->split[2]);
        if (this_rdc.rate != INT_MAX && this_rdc.dist != INT64_MAX &&
            rd_cost->rate != INT_MAX && rd_cost->dist != INT64_MAX) {
          rd_cost->rate += this_rdc.rate;
          rd_cost->dist += this_rdc.dist;
        }
        nonrd_select_partition(cpi, td, tile_data, mi + hbs * mis + hbs, tp,
                               mi_row + hbs, mi_col + hbs, subsize,
                               output_enabled, &this_rdc, pc_tree->split[3]);
        if (this_rdc.rate != INT_MAX && this_rdc.dist != INT64_MAX &&
            rd_cost->rate != INT_MAX && rd_cost->dist != INT64_MAX) {
          rd_cost->rate += this_rdc.rate;
          rd_cost->dist += this_rdc.dist;
        }
        break;
      default:
        assert(0 && "Invalid partition type.");
        break;
    }
  }

  if (bsize == BLOCK_64X64 && output_enabled)
    encode_sb_rt(cpi, td, tile_info, tp, mi_row, mi_col, 1, bsize, pc_tree);
}


static void nonrd_use_partition(VP9_COMP *cpi,
                                ThreadData *td,
                                TileDataEnc *tile_data,
                                MODE_INFO **mi,
                                TOKENEXTRA **tp,
                                int mi_row, int mi_col,
                                BLOCK_SIZE bsize, int output_enabled,
                                RD_COST *dummy_cost, PC_TREE *pc_tree) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  const int bsl = b_width_log2_lookup[bsize], hbs = (1 << bsl) / 4;
  const int mis = cm->mi_stride;
  PARTITION_TYPE partition;
  BLOCK_SIZE subsize;

  if (mi_row >= cm->mi_rows || mi_col >= cm->mi_cols)
    return;

  subsize = (bsize >= BLOCK_8X8) ? mi[0]->mbmi.sb_type : BLOCK_4X4;
  partition = partition_lookup[bsl][subsize];

  if (output_enabled && bsize != BLOCK_4X4) {
    int ctx = partition_plane_context(xd, mi_row, mi_col, bsize);
    td->counts->partition[ctx][partition]++;
  }

  switch (partition) {
    case PARTITION_NONE:
      pc_tree->none.pred_pixel_ready = 1;
      nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, dummy_cost,
                          subsize, &pc_tree->none);
      pc_tree->none.mic.mbmi = xd->mi[0]->mbmi;
      pc_tree->none.mbmi_ext = *x->mbmi_ext;
      pc_tree->none.skip_txfm[0] = x->skip_txfm[0];
      pc_tree->none.skip = x->skip;
      encode_b_rt(cpi, td, tile_info, tp, mi_row, mi_col, output_enabled,
                  subsize, &pc_tree->none);
      break;
    case PARTITION_VERT:
      pc_tree->vertical[0].pred_pixel_ready = 1;
      nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, dummy_cost,
                          subsize, &pc_tree->vertical[0]);
      pc_tree->vertical[0].mic.mbmi = xd->mi[0]->mbmi;
      pc_tree->vertical[0].mbmi_ext = *x->mbmi_ext;
      pc_tree->vertical[0].skip_txfm[0] = x->skip_txfm[0];
      pc_tree->vertical[0].skip = x->skip;
      encode_b_rt(cpi, td, tile_info, tp, mi_row, mi_col, output_enabled,
                  subsize, &pc_tree->vertical[0]);
      if (mi_col + hbs < cm->mi_cols && bsize > BLOCK_8X8) {
        pc_tree->vertical[1].pred_pixel_ready = 1;
        nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col + hbs,
                            dummy_cost, subsize, &pc_tree->vertical[1]);
        pc_tree->vertical[1].mic.mbmi = xd->mi[0]->mbmi;
        pc_tree->vertical[1].mbmi_ext = *x->mbmi_ext;
        pc_tree->vertical[1].skip_txfm[0] = x->skip_txfm[0];
        pc_tree->vertical[1].skip = x->skip;
        encode_b_rt(cpi, td, tile_info, tp, mi_row, mi_col + hbs,
                    output_enabled, subsize, &pc_tree->vertical[1]);
      }
      break;
    case PARTITION_HORZ:
      pc_tree->horizontal[0].pred_pixel_ready = 1;
      nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, dummy_cost,
                          subsize, &pc_tree->horizontal[0]);
      pc_tree->horizontal[0].mic.mbmi = xd->mi[0]->mbmi;
      pc_tree->horizontal[0].mbmi_ext = *x->mbmi_ext;
      pc_tree->horizontal[0].skip_txfm[0] = x->skip_txfm[0];
      pc_tree->horizontal[0].skip = x->skip;
      encode_b_rt(cpi, td, tile_info, tp, mi_row, mi_col, output_enabled,
                  subsize, &pc_tree->horizontal[0]);

      if (mi_row + hbs < cm->mi_rows && bsize > BLOCK_8X8) {
        pc_tree->horizontal[1].pred_pixel_ready = 1;
        nonrd_pick_sb_modes(cpi, tile_data, x, mi_row + hbs, mi_col,
                            dummy_cost, subsize, &pc_tree->horizontal[1]);
        pc_tree->horizontal[1].mic.mbmi = xd->mi[0]->mbmi;
        pc_tree->horizontal[1].mbmi_ext = *x->mbmi_ext;
        pc_tree->horizontal[1].skip_txfm[0] = x->skip_txfm[0];
        pc_tree->horizontal[1].skip = x->skip;
        encode_b_rt(cpi, td, tile_info, tp, mi_row + hbs, mi_col,
                    output_enabled, subsize, &pc_tree->horizontal[1]);
      }
      break;
    case PARTITION_SPLIT:
      subsize = get_subsize(bsize, PARTITION_SPLIT);
      if (bsize == BLOCK_8X8) {
        nonrd_pick_sb_modes(cpi, tile_data, x, mi_row, mi_col, dummy_cost,
                            subsize, pc_tree->leaf_split[0]);
        encode_b_rt(cpi, td, tile_info, tp, mi_row, mi_col,
                    output_enabled, subsize, pc_tree->leaf_split[0]);
      } else {
        nonrd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                            subsize, output_enabled, dummy_cost,
                            pc_tree->split[0]);
        nonrd_use_partition(cpi, td, tile_data, mi + hbs, tp,
                            mi_row, mi_col + hbs, subsize, output_enabled,
                            dummy_cost, pc_tree->split[1]);
        nonrd_use_partition(cpi, td, tile_data, mi + hbs * mis, tp,
                            mi_row + hbs, mi_col, subsize, output_enabled,
                            dummy_cost, pc_tree->split[2]);
        nonrd_use_partition(cpi, td, tile_data, mi + hbs * mis + hbs, tp,
                            mi_row + hbs, mi_col + hbs, subsize, output_enabled,
                            dummy_cost, pc_tree->split[3]);
      }
      break;
    default:
      assert(0 && "Invalid partition type.");
      break;
  }

  if (partition != PARTITION_SPLIT || bsize == BLOCK_8X8)
    update_partition_context(xd, mi_row, mi_col, subsize, bsize);
}

static void encode_nonrd_sb_row(VP9_COMP *cpi,
                                ThreadData *td,
                                TileDataEnc *tile_data,
                                int mi_row,
                                TOKENEXTRA **tp) {
  SPEED_FEATURES *const sf = &cpi->sf;
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  int mi_col;

  // Initialize the left context for the new SB row
  memset(&xd->left_context, 0, sizeof(xd->left_context));
  memset(xd->left_seg_context, 0, sizeof(xd->left_seg_context));

  // Code each SB in the row
  for (mi_col = tile_info->mi_col_start; mi_col < tile_info->mi_col_end;
       mi_col += MI_BLOCK_SIZE) {
    const struct segmentation *const seg = &cm->seg;
    RD_COST dummy_rdc;
    const int idx_str = cm->mi_stride * mi_row + mi_col;
    MODE_INFO **mi = cm->mi_grid_visible + idx_str;
    PARTITION_SEARCH_TYPE partition_search_type = sf->partition_search_type;
    BLOCK_SIZE bsize = BLOCK_64X64;
    int seg_skip = 0;
    x->source_variance = UINT_MAX;
    vp9_zero(x->pred_mv);
    vp9_rd_cost_init(&dummy_rdc);
    x->color_sensitivity[0] = 0;
    x->color_sensitivity[1] = 0;

    if (seg->enabled) {
      const uint8_t *const map = seg->update_map ? cpi->segmentation_map
                                                 : cm->last_frame_seg_map;
      int segment_id = get_segment_id(cm, map, BLOCK_64X64, mi_row, mi_col);
      seg_skip = segfeature_active(seg, segment_id, SEG_LVL_SKIP);
      if (seg_skip) {
        partition_search_type = FIXED_PARTITION;
      }
    }

    // Set the partition type of the 64X64 block
    switch (partition_search_type) {
      case VAR_BASED_PARTITION:
        // TODO(jingning, marpan): The mode decision and encoding process
        // support both intra and inter sub8x8 block coding for RTC mode.
        // Tune the thresholds accordingly to use sub8x8 block coding for
        // coding performance improvement.
        choose_partitioning(cpi, tile_info, x, mi_row, mi_col);
        nonrd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                            BLOCK_64X64, 1, &dummy_rdc, td->pc_root);
        break;
      case SOURCE_VAR_BASED_PARTITION:
        set_source_var_based_partition(cpi, tile_info, x, mi, mi_row, mi_col);
        nonrd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                            BLOCK_64X64, 1, &dummy_rdc, td->pc_root);
        break;
      case FIXED_PARTITION:
        if (!seg_skip)
          bsize = sf->always_this_block_size;
        set_fixed_partitioning(cpi, tile_info, mi, mi_row, mi_col, bsize);
        nonrd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                            BLOCK_64X64, 1, &dummy_rdc, td->pc_root);
        break;
      case REFERENCE_PARTITION:
        set_offsets(cpi, tile_info, x, mi_row, mi_col, BLOCK_64X64);
        if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ && cm->seg.enabled &&
            xd->mi[0]->mbmi.segment_id) {
          // Use lower max_partition_size for low resoultions.
          if (cm->width <= 352 && cm->height <= 288)
            x->max_partition_size = BLOCK_32X32;
          else
            x->max_partition_size = BLOCK_64X64;
          x->min_partition_size = BLOCK_8X8;
          nonrd_pick_partition(cpi, td, tile_data, tp, mi_row, mi_col,
                               BLOCK_64X64, &dummy_rdc, 1,
                               INT64_MAX, td->pc_root);
        } else {
          choose_partitioning(cpi, tile_info, x, mi_row, mi_col);
          // TODO(marpan): Seems like nonrd_select_partition does not support
          // 4x4 partition. Since 4x4 is used on key frame, use this switch
          // for now.
          if (cm->frame_type == KEY_FRAME)
            nonrd_use_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                                BLOCK_64X64, 1, &dummy_rdc, td->pc_root);
          else
            nonrd_select_partition(cpi, td, tile_data, mi, tp, mi_row, mi_col,
                                   BLOCK_64X64, 1, &dummy_rdc, td->pc_root);
        }

        break;
      default:
        assert(0);
        break;
    }
  }
}
// end RTC play code

static int set_var_thresh_from_histogram(VP9_COMP *cpi) {
  const SPEED_FEATURES *const sf = &cpi->sf;
  const VP9_COMMON *const cm = &cpi->common;

  const uint8_t *src = cpi->Source->y_buffer;
  const uint8_t *last_src = cpi->Last_Source->y_buffer;
  const int src_stride = cpi->Source->y_stride;
  const int last_stride = cpi->Last_Source->y_stride;

  // Pick cutoff threshold
  const int cutoff = (MIN(cm->width, cm->height) >= 720) ?
      (cm->MBs * VAR_HIST_LARGE_CUT_OFF / 100) :
      (cm->MBs * VAR_HIST_SMALL_CUT_OFF / 100);
  DECLARE_ALIGNED(16, int, hist[VAR_HIST_BINS]);
  diff *var16 = cpi->source_diff_var;

  int sum = 0;
  int i, j;

  memset(hist, 0, VAR_HIST_BINS * sizeof(hist[0]));

  for (i = 0; i < cm->mb_rows; i++) {
    for (j = 0; j < cm->mb_cols; j++) {
#if CONFIG_VP9_HIGHBITDEPTH
      if (cm->use_highbitdepth) {
        switch (cm->bit_depth) {
          case VPX_BITS_8:
            vpx_highbd_8_get16x16var(src, src_stride, last_src, last_stride,
                                   &var16->sse, &var16->sum);
            break;
          case VPX_BITS_10:
            vpx_highbd_10_get16x16var(src, src_stride, last_src, last_stride,
                                    &var16->sse, &var16->sum);
            break;
          case VPX_BITS_12:
            vpx_highbd_12_get16x16var(src, src_stride, last_src, last_stride,
                                      &var16->sse, &var16->sum);
            break;
          default:
            assert(0 && "cm->bit_depth should be VPX_BITS_8, VPX_BITS_10"
                   " or VPX_BITS_12");
            return -1;
        }
      } else {
        vpx_get16x16var(src, src_stride, last_src, last_stride,
                        &var16->sse, &var16->sum);
      }
#else
      vpx_get16x16var(src, src_stride, last_src, last_stride,
                      &var16->sse, &var16->sum);
#endif  // CONFIG_VP9_HIGHBITDEPTH
      var16->var = var16->sse -
          (((uint32_t)var16->sum * var16->sum) >> 8);

      if (var16->var >= VAR_HIST_MAX_BG_VAR)
        hist[VAR_HIST_BINS - 1]++;
      else
        hist[var16->var / VAR_HIST_FACTOR]++;

      src += 16;
      last_src += 16;
      var16++;
    }

    src = src - cm->mb_cols * 16 + 16 * src_stride;
    last_src = last_src - cm->mb_cols * 16 + 16 * last_stride;
  }

  cpi->source_var_thresh = 0;

  if (hist[VAR_HIST_BINS - 1] < cutoff) {
    for (i = 0; i < VAR_HIST_BINS - 1; i++) {
      sum += hist[i];

      if (sum > cutoff) {
        cpi->source_var_thresh = (i + 1) * VAR_HIST_FACTOR;
        return 0;
      }
    }
  }

  return sf->search_type_check_frequency;
}

static void source_var_based_partition_search_method(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  SPEED_FEATURES *const sf = &cpi->sf;

  if (cm->frame_type == KEY_FRAME) {
    // For key frame, use SEARCH_PARTITION.
    sf->partition_search_type = SEARCH_PARTITION;
  } else if (cm->intra_only) {
    sf->partition_search_type = FIXED_PARTITION;
  } else {
    if (cm->last_width != cm->width || cm->last_height != cm->height) {
      if (cpi->source_diff_var)
        vpx_free(cpi->source_diff_var);

      CHECK_MEM_ERROR(cm, cpi->source_diff_var,
                      vpx_calloc(cm->MBs, sizeof(diff)));
    }

    if (!cpi->frames_till_next_var_check)
      cpi->frames_till_next_var_check = set_var_thresh_from_histogram(cpi);

    if (cpi->frames_till_next_var_check > 0) {
      sf->partition_search_type = FIXED_PARTITION;
      cpi->frames_till_next_var_check--;
    }
  }
}

static int get_skip_encode_frame(const VP9_COMMON *cm, ThreadData *const td) {
  unsigned int intra_count = 0, inter_count = 0;
  int j;

  for (j = 0; j < INTRA_INTER_CONTEXTS; ++j) {
    intra_count += td->counts->intra_inter[j][0];
    inter_count += td->counts->intra_inter[j][1];
  }

  return (intra_count << 2) < inter_count &&
         cm->frame_type != KEY_FRAME &&
         cm->show_frame;
}

void vp9_init_tile_data(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  const int tile_cols = 1 << cm->log2_tile_cols;
  const int tile_rows = 1 << cm->log2_tile_rows;
  int tile_col, tile_row;
  TOKENEXTRA *pre_tok = cpi->tile_tok[0][0];
  int tile_tok = 0;

  if (cpi->tile_data == NULL || cpi->allocated_tiles < tile_cols * tile_rows) {
    if (cpi->tile_data != NULL)
      vpx_free(cpi->tile_data);
    CHECK_MEM_ERROR(cm, cpi->tile_data,
        vpx_malloc(tile_cols * tile_rows * sizeof(*cpi->tile_data)));
    cpi->allocated_tiles = tile_cols * tile_rows;

    for (tile_row = 0; tile_row < tile_rows; ++tile_row)
      for (tile_col = 0; tile_col < tile_cols; ++tile_col) {
        TileDataEnc *tile_data =
            &cpi->tile_data[tile_row * tile_cols + tile_col];
        int i, j;
        for (i = 0; i < BLOCK_SIZES; ++i) {
          for (j = 0; j < MAX_MODES; ++j) {
            tile_data->thresh_freq_fact[i][j] = 32;
            tile_data->mode_map[i][j] = j;
          }
        }
      }
  }

  for (tile_row = 0; tile_row < tile_rows; ++tile_row) {
    for (tile_col = 0; tile_col < tile_cols; ++tile_col) {
      TileInfo *tile_info =
          &cpi->tile_data[tile_row * tile_cols + tile_col].tile_info;
      vp9_tile_init(tile_info, cm, tile_row, tile_col);

      cpi->tile_tok[tile_row][tile_col] = pre_tok + tile_tok;
      pre_tok = cpi->tile_tok[tile_row][tile_col];
      tile_tok = allocated_tokens(*tile_info);
    }
  }
}

void vp9_encode_tile(VP9_COMP *cpi, ThreadData *td,
                     int tile_row, int tile_col) {
  VP9_COMMON *const cm = &cpi->common;
  const int tile_cols = 1 << cm->log2_tile_cols;
  TileDataEnc *this_tile =
      &cpi->tile_data[tile_row * tile_cols + tile_col];
  const TileInfo * const tile_info = &this_tile->tile_info;
  TOKENEXTRA *tok = cpi->tile_tok[tile_row][tile_col];
  int mi_row;

  for (mi_row = tile_info->mi_row_start; mi_row < tile_info->mi_row_end;
       mi_row += MI_BLOCK_SIZE) {
    if (cpi->sf.use_nonrd_pick_mode)
      encode_nonrd_sb_row(cpi, td, this_tile, mi_row, &tok);
    else
      encode_rd_sb_row(cpi, td, this_tile, mi_row, &tok);
  }
  cpi->tok_count[tile_row][tile_col] =
      (unsigned int)(tok - cpi->tile_tok[tile_row][tile_col]);
  assert(tok - cpi->tile_tok[tile_row][tile_col] <=
      allocated_tokens(*tile_info));
}

static void encode_tiles(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;
  const int tile_cols = 1 << cm->log2_tile_cols;
  const int tile_rows = 1 << cm->log2_tile_rows;
  int tile_col, tile_row;

  vp9_init_tile_data(cpi);

  for (tile_row = 0; tile_row < tile_rows; ++tile_row)
    for (tile_col = 0; tile_col < tile_cols; ++tile_col)
      vp9_encode_tile(cpi, &cpi->td, tile_row, tile_col);
}

#if CONFIG_FP_MB_STATS
static int input_fpmb_stats(FIRSTPASS_MB_STATS *firstpass_mb_stats,
                            VP9_COMMON *cm, uint8_t **this_frame_mb_stats) {
  uint8_t *mb_stats_in = firstpass_mb_stats->mb_stats_start +
      cm->current_video_frame * cm->MBs * sizeof(uint8_t);

  if (mb_stats_in > firstpass_mb_stats->mb_stats_end)
    return EOF;

  *this_frame_mb_stats = mb_stats_in;

  return 1;
}
#endif

static void encode_frame_internal(VP9_COMP *cpi) {
  SPEED_FEATURES *const sf = &cpi->sf;
  ThreadData *const td = &cpi->td;
  MACROBLOCK *const x = &td->mb;
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCKD *const xd = &x->e_mbd;
  RD_COUNTS *const rdc = &cpi->td.rd_counts;

  xd->mi = cm->mi_grid_visible;
  xd->mi[0] = cm->mi;

  vp9_zero(*td->counts);
  vp9_zero(rdc->coef_counts);
  vp9_zero(rdc->comp_pred_diff);
  vp9_zero(rdc->filter_diff);

  xd->lossless = cm->base_qindex == 0 &&
                 cm->y_dc_delta_q == 0 &&
                 cm->uv_dc_delta_q == 0 &&
                 cm->uv_ac_delta_q == 0;

#if CONFIG_VP9_HIGHBITDEPTH
  if (cm->use_highbitdepth)
    x->fwd_txm4x4 = xd->lossless ? vp9_highbd_fwht4x4 : vpx_highbd_fdct4x4;
  else
    x->fwd_txm4x4 = xd->lossless ? vp9_fwht4x4 : vpx_fdct4x4;
  x->highbd_itxm_add = xd->lossless ? vp9_highbd_iwht4x4_add :
                                      vp9_highbd_idct4x4_add;
#else
  x->fwd_txm4x4 = xd->lossless ? vp9_fwht4x4 : vpx_fdct4x4;
#endif  // CONFIG_VP9_HIGHBITDEPTH
  x->itxm_add = xd->lossless ? vp9_iwht4x4_add : vp9_idct4x4_add;

  if (xd->lossless)
    x->optimize = 0;

  cm->tx_mode = select_tx_mode(cpi, xd);

  vp9_frame_init_quantizer(cpi);

  vp9_initialize_rd_consts(cpi);
  vp9_initialize_me_consts(cpi, x, cm->base_qindex);
  init_encode_frame_mb_context(cpi);
  cm->use_prev_frame_mvs = !cm->error_resilient_mode &&
                           cm->width == cm->last_width &&
                           cm->height == cm->last_height &&
                           !cm->intra_only &&
                           cm->last_show_frame;
  // Special case: set prev_mi to NULL when the previous mode info
  // context cannot be used.
  cm->prev_mi = cm->use_prev_frame_mvs ?
                cm->prev_mip + cm->mi_stride + 1 : NULL;

  x->quant_fp = cpi->sf.use_quant_fp;
  vp9_zero(x->skip_txfm);
  if (sf->use_nonrd_pick_mode) {
    // Initialize internal buffer pointers for rtc coding, where non-RD
    // mode decision is used and hence no buffer pointer swap needed.
    int i;
    struct macroblock_plane *const p = x->plane;
    struct macroblockd_plane *const pd = xd->plane;
    PICK_MODE_CONTEXT *ctx = &cpi->td.pc_root->none;

    for (i = 0; i < MAX_MB_PLANE; ++i) {
      p[i].coeff = ctx->coeff_pbuf[i][0];
      p[i].qcoeff = ctx->qcoeff_pbuf[i][0];
      pd[i].dqcoeff = ctx->dqcoeff_pbuf[i][0];
      p[i].eobs = ctx->eobs_pbuf[i][0];
    }
    vp9_zero(x->zcoeff_blk);

    if (cm->frame_type != KEY_FRAME && cpi->rc.frames_since_golden == 0)
      cpi->ref_frame_flags &= (~VP9_GOLD_FLAG);

    if (sf->partition_search_type == SOURCE_VAR_BASED_PARTITION)
      source_var_based_partition_search_method(cpi);
  }

  {
    struct vpx_usec_timer emr_timer;
    vpx_usec_timer_start(&emr_timer);

#if CONFIG_FP_MB_STATS
  if (cpi->use_fp_mb_stats) {
    input_fpmb_stats(&cpi->twopass.firstpass_mb_stats, cm,
                     &cpi->twopass.this_frame_mb_stats);
  }
#endif

    // If allowed, encoding tiles in parallel with one thread handling one tile.
    if (MIN(cpi->oxcf.max_threads, 1 << cm->log2_tile_cols) > 1)
      vp9_encode_tiles_mt(cpi);
    else
      encode_tiles(cpi);

    vpx_usec_timer_mark(&emr_timer);
    cpi->time_encode_sb_row += vpx_usec_timer_elapsed(&emr_timer);
  }

  sf->skip_encode_frame = sf->skip_encode_sb ?
      get_skip_encode_frame(cm, td) : 0;

#if 0
  // Keep record of the total distortion this time around for future use
  cpi->last_frame_distortion = cpi->frame_distortion;
#endif
}

static INTERP_FILTER get_interp_filter(
    const int64_t threshes[SWITCHABLE_FILTER_CONTEXTS], int is_alt_ref) {
  if (!is_alt_ref &&
      threshes[EIGHTTAP_SMOOTH] > threshes[EIGHTTAP] &&
      threshes[EIGHTTAP_SMOOTH] > threshes[EIGHTTAP_SHARP] &&
      threshes[EIGHTTAP_SMOOTH] > threshes[SWITCHABLE - 1]) {
    return EIGHTTAP_SMOOTH;
  } else if (threshes[EIGHTTAP_SHARP] > threshes[EIGHTTAP] &&
             threshes[EIGHTTAP_SHARP] > threshes[SWITCHABLE - 1]) {
    return EIGHTTAP_SHARP;
  } else if (threshes[EIGHTTAP] > threshes[SWITCHABLE - 1]) {
    return EIGHTTAP;
  } else {
    return SWITCHABLE;
  }
}

void vp9_encode_frame(VP9_COMP *cpi) {
  VP9_COMMON *const cm = &cpi->common;

  // In the longer term the encoder should be generalized to match the
  // decoder such that we allow compound where one of the 3 buffers has a
  // different sign bias and that buffer is then the fixed ref. However, this
  // requires further work in the rd loop. For now the only supported encoder
  // side behavior is where the ALT ref buffer has opposite sign bias to
  // the other two.
  if (!frame_is_intra_only(cm)) {
    if ((cm->ref_frame_sign_bias[ALTREF_FRAME] ==
             cm->ref_frame_sign_bias[GOLDEN_FRAME]) ||
        (cm->ref_frame_sign_bias[ALTREF_FRAME] ==
             cm->ref_frame_sign_bias[LAST_FRAME])) {
      cpi->allow_comp_inter_inter = 0;
    } else {
      cpi->allow_comp_inter_inter = 1;
      cm->comp_fixed_ref = ALTREF_FRAME;
      cm->comp_var_ref[0] = LAST_FRAME;
      cm->comp_var_ref[1] = GOLDEN_FRAME;
    }
  }

  if (cpi->sf.frame_parameter_update) {
    int i;
    RD_OPT *const rd_opt = &cpi->rd;
    FRAME_COUNTS *counts = cpi->td.counts;
    RD_COUNTS *const rdc = &cpi->td.rd_counts;

    // This code does a single RD pass over the whole frame assuming
    // either compound, single or hybrid prediction as per whatever has
    // worked best for that type of frame in the past.
    // It also predicts whether another coding mode would have worked
    // better that this coding mode. If that is the case, it remembers
    // that for subsequent frames.
    // It does the same analysis for transform size selection also.
    const MV_REFERENCE_FRAME frame_type = get_frame_type(cpi);
    int64_t *const mode_thrs = rd_opt->prediction_type_threshes[frame_type];
    int64_t *const filter_thrs = rd_opt->filter_threshes[frame_type];
    const int is_alt_ref = frame_type == ALTREF_FRAME;

    /* prediction (compound, single or hybrid) mode selection */
    if (is_alt_ref || !cpi->allow_comp_inter_inter)
      cm->reference_mode = SINGLE_REFERENCE;
    else if (mode_thrs[COMPOUND_REFERENCE] > mode_thrs[SINGLE_REFERENCE] &&
             mode_thrs[COMPOUND_REFERENCE] >
                 mode_thrs[REFERENCE_MODE_SELECT] &&
             check_dual_ref_flags(cpi) &&
             cpi->static_mb_pct == 100)
      cm->reference_mode = COMPOUND_REFERENCE;
    else if (mode_thrs[SINGLE_REFERENCE] > mode_thrs[REFERENCE_MODE_SELECT])
      cm->reference_mode = SINGLE_REFERENCE;
    else
      cm->reference_mode = REFERENCE_MODE_SELECT;

    if (cm->interp_filter == SWITCHABLE)
      cm->interp_filter = get_interp_filter(filter_thrs, is_alt_ref);

    encode_frame_internal(cpi);

    for (i = 0; i < REFERENCE_MODES; ++i)
      mode_thrs[i] = (mode_thrs[i] + rdc->comp_pred_diff[i] / cm->MBs) / 2;

    for (i = 0; i < SWITCHABLE_FILTER_CONTEXTS; ++i)
      filter_thrs[i] = (filter_thrs[i] + rdc->filter_diff[i] / cm->MBs) / 2;

    if (cm->reference_mode == REFERENCE_MODE_SELECT) {
      int single_count_zero = 0;
      int comp_count_zero = 0;

      for (i = 0; i < COMP_INTER_CONTEXTS; i++) {
        single_count_zero += counts->comp_inter[i][0];
        comp_count_zero += counts->comp_inter[i][1];
      }

      if (comp_count_zero == 0) {
        cm->reference_mode = SINGLE_REFERENCE;
        vp9_zero(counts->comp_inter);
      } else if (single_count_zero == 0) {
        cm->reference_mode = COMPOUND_REFERENCE;
        vp9_zero(counts->comp_inter);
      }
    }

    if (cm->tx_mode == TX_MODE_SELECT) {
      int count4x4 = 0;
      int count8x8_lp = 0, count8x8_8x8p = 0;
      int count16x16_16x16p = 0, count16x16_lp = 0;
      int count32x32 = 0;

      for (i = 0; i < TX_SIZE_CONTEXTS; ++i) {
        count4x4 += counts->tx.p32x32[i][TX_4X4];
        count4x4 += counts->tx.p16x16[i][TX_4X4];
        count4x4 += counts->tx.p8x8[i][TX_4X4];

        count8x8_lp += counts->tx.p32x32[i][TX_8X8];
        count8x8_lp += counts->tx.p16x16[i][TX_8X8];
        count8x8_8x8p += counts->tx.p8x8[i][TX_8X8];

        count16x16_16x16p += counts->tx.p16x16[i][TX_16X16];
        count16x16_lp += counts->tx.p32x32[i][TX_16X16];
        count32x32 += counts->tx.p32x32[i][TX_32X32];
      }
      if (count4x4 == 0 && count16x16_lp == 0 && count16x16_16x16p == 0 &&
          count32x32 == 0) {
        cm->tx_mode = ALLOW_8X8;
        reset_skip_tx_size(cm, TX_8X8);
      } else if (count8x8_8x8p == 0 && count16x16_16x16p == 0 &&
                 count8x8_lp == 0 && count16x16_lp == 0 && count32x32 == 0) {
        cm->tx_mode = ONLY_4X4;
        reset_skip_tx_size(cm, TX_4X4);
      } else if (count8x8_lp == 0 && count16x16_lp == 0 && count4x4 == 0) {
        cm->tx_mode = ALLOW_32X32;
      } else if (count32x32 == 0 && count8x8_lp == 0 && count4x4 == 0) {
        cm->tx_mode = ALLOW_16X16;
        reset_skip_tx_size(cm, TX_16X16);
      }
    }
  } else {
    cm->reference_mode = SINGLE_REFERENCE;
    encode_frame_internal(cpi);
  }
}

static void sum_intra_stats(FRAME_COUNTS *counts, const MODE_INFO *mi) {
  const PREDICTION_MODE y_mode = mi->mbmi.mode;
  const PREDICTION_MODE uv_mode = mi->mbmi.uv_mode;
  const BLOCK_SIZE bsize = mi->mbmi.sb_type;

  if (bsize < BLOCK_8X8) {
    int idx, idy;
    const int num_4x4_w = num_4x4_blocks_wide_lookup[bsize];
    const int num_4x4_h = num_4x4_blocks_high_lookup[bsize];
    for (idy = 0; idy < 2; idy += num_4x4_h)
      for (idx = 0; idx < 2; idx += num_4x4_w)
        ++counts->y_mode[0][mi->bmi[idy * 2 + idx].as_mode];
  } else {
    ++counts->y_mode[size_group_lookup[bsize]][y_mode];
  }

  ++counts->uv_mode[y_mode][uv_mode];
}

static void encode_superblock(VP9_COMP *cpi, ThreadData *td,
                              TOKENEXTRA **t, int output_enabled,
                              int mi_row, int mi_col, BLOCK_SIZE bsize,
                              PICK_MODE_CONTEXT *ctx) {
  VP9_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MODE_INFO **mi_8x8 = xd->mi;
  MODE_INFO *mi = mi_8x8[0];
  MB_MODE_INFO *mbmi = &mi->mbmi;
  const int seg_skip = segfeature_active(&cm->seg, mbmi->segment_id,
                                         SEG_LVL_SKIP);
  const int mis = cm->mi_stride;
  const int mi_width = num_8x8_blocks_wide_lookup[bsize];
  const int mi_height = num_8x8_blocks_high_lookup[bsize];

  x->skip_recode = !x->select_tx_size && mbmi->sb_type >= BLOCK_8X8 &&
                   cpi->oxcf.aq_mode != COMPLEXITY_AQ &&
                   cpi->oxcf.aq_mode != CYCLIC_REFRESH_AQ &&
                   cpi->sf.allow_skip_recode;

  if (!x->skip_recode && !cpi->sf.use_nonrd_pick_mode)
    memset(x->skip_txfm, 0, sizeof(x->skip_txfm));

  x->skip_optimize = ctx->is_coded;
  ctx->is_coded = 1;
  x->use_lp32x32fdct = cpi->sf.use_lp32x32fdct;
  x->skip_encode = (!output_enabled && cpi->sf.skip_encode_frame &&
                    x->q_index < QIDX_SKIP_THRESH);

  if (x->skip_encode)
    return;

  set_ref_ptrs(cm, xd, mbmi->ref_frame[0], mbmi->ref_frame[1]);

  if (!is_inter_block(mbmi)) {
    int plane;
    mbmi->skip = 1;
    for (plane = 0; plane < MAX_MB_PLANE; ++plane)
      vp9_encode_intra_block_plane(x, MAX(bsize, BLOCK_8X8), plane);
    if (output_enabled)
      sum_intra_stats(td->counts, mi);
    vp9_tokenize_sb(cpi, td, t, !output_enabled, MAX(bsize, BLOCK_8X8));
  } else {
    int ref;
    const int is_compound = has_second_ref(mbmi);
    for (ref = 0; ref < 1 + is_compound; ++ref) {
      YV12_BUFFER_CONFIG *cfg = get_ref_frame_buffer(cpi,
                                                     mbmi->ref_frame[ref]);
      assert(cfg != NULL);
      vp9_setup_pre_planes(xd, ref, cfg, mi_row, mi_col,
                           &xd->block_refs[ref]->sf);
    }
    if (!(cpi->sf.reuse_inter_pred_sby && ctx->pred_pixel_ready) || seg_skip)
      vp9_build_inter_predictors_sby(xd, mi_row, mi_col, MAX(bsize, BLOCK_8X8));

    vp9_build_inter_predictors_sbuv(xd, mi_row, mi_col, MAX(bsize, BLOCK_8X8));

    vp9_encode_sb(x, MAX(bsize, BLOCK_8X8));
    vp9_tokenize_sb(cpi, td, t, !output_enabled, MAX(bsize, BLOCK_8X8));
  }

  if (output_enabled) {
    if (cm->tx_mode == TX_MODE_SELECT &&
        mbmi->sb_type >= BLOCK_8X8  &&
        !(is_inter_block(mbmi) && (mbmi->skip || seg_skip))) {
      ++get_tx_counts(max_txsize_lookup[bsize], get_tx_size_context(xd),
                      &td->counts->tx)[mbmi->tx_size];
    } else {
      int x, y;
      TX_SIZE tx_size;
      // The new intra coding scheme requires no change of transform size
      if (is_inter_block(&mi->mbmi)) {
        tx_size = MIN(tx_mode_to_biggest_tx_size[cm->tx_mode],
                      max_txsize_lookup[bsize]);
      } else {
        tx_size = (bsize >= BLOCK_8X8) ? mbmi->tx_size : TX_4X4;
      }

      for (y = 0; y < mi_height; y++)
        for (x = 0; x < mi_width; x++)
          if (mi_col + x < cm->mi_cols && mi_row + y < cm->mi_rows)
            mi_8x8[mis * y + x]->mbmi.tx_size = tx_size;
    }
    ++td->counts->tx.tx_totals[mbmi->tx_size];
    ++td->counts->tx.tx_totals[get_uv_tx_size(mbmi, &xd->plane[1])];
  }
}
