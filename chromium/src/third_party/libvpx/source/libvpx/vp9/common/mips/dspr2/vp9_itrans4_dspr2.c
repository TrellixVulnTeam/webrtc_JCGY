/*
 *  Copyright (c) 2013 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <stdio.h>

#include "./vpx_config.h"
#include "./vp9_rtcd.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_idct.h"
#include "vp9/common/mips/dspr2/vp9_common_dspr2.h"
#include "vpx_dsp/txfm_common.h"
#include "vpx_ports/mem.h"

#if HAVE_DSPR2
static void vp9_idct4_rows_dspr2(const int16_t *input, int16_t *output) {
  int16_t   step_0, step_1, step_2, step_3;
  int       Temp0, Temp1, Temp2, Temp3;
  const int const_2_power_13 = 8192;
  int       i;

  for (i = 4; i--; ) {
    __asm__ __volatile__ (
        /*
          temp_1 = (input[0] + input[2]) * cospi_16_64;
          step_0 = dct_const_round_shift(temp_1);

          temp_2 = (input[0] - input[2]) * cospi_16_64;
          step_1 = dct_const_round_shift(temp_2);
        */
        "lh       %[Temp0],             0(%[input])                     \n\t"
        "lh       %[Temp1],             4(%[input])                     \n\t"
        "mtlo     %[const_2_power_13],  $ac0                            \n\t"
        "mthi     $zero,                $ac0                            \n\t"
        "mtlo     %[const_2_power_13],  $ac1                            \n\t"
        "mthi     $zero,                $ac1                            \n\t"
        "add      %[Temp2],             %[Temp0],       %[Temp1]        \n\t"
        "sub      %[Temp3],             %[Temp0],       %[Temp1]        \n\t"
        "madd     $ac0,                 %[Temp2],       %[cospi_16_64]  \n\t"
        "lh       %[Temp0],             2(%[input])                     \n\t"
        "lh       %[Temp1],             6(%[input])                     \n\t"
        "extp     %[step_0],            $ac0,           31              \n\t"
        "mtlo     %[const_2_power_13],  $ac0                            \n\t"
        "mthi     $zero,                $ac0                            \n\t"

        "madd     $ac1,                 %[Temp3],       %[cospi_16_64]  \n\t"
        "extp     %[step_1],            $ac1,           31              \n\t"
        "mtlo     %[const_2_power_13],  $ac1                            \n\t"
        "mthi     $zero,                $ac1                            \n\t"

        /*
          temp1 = input[1] * cospi_24_64 - input[3] * cospi_8_64;
          step_2 = dct_const_round_shift(temp1);
        */
        "madd     $ac0,                 %[Temp0],       %[cospi_24_64]  \n\t"
        "msub     $ac0,                 %[Temp1],       %[cospi_8_64]   \n\t"
        "extp     %[step_2],            $ac0,           31              \n\t"

        /*
          temp2 = input[1] * cospi_8_64 + input[3] * cospi_24_64;
          step_3 = dct_const_round_shift(temp2);
        */
        "madd     $ac1,                 %[Temp0],       %[cospi_8_64]   \n\t"
        "madd     $ac1,                 %[Temp1],       %[cospi_24_64]  \n\t"
        "extp     %[step_3],            $ac1,           31              \n\t"

        /*
          output[0]  = step_0 + step_3;
          output[4]  = step_1 + step_2;
          output[8]  = step_1 - step_2;
          output[12] = step_0 - step_3;
        */
        "add      %[Temp0],             %[step_0],      %[step_3]       \n\t"
        "sh       %[Temp0],             0(%[output])                    \n\t"

        "add      %[Temp1],             %[step_1],      %[step_2]       \n\t"
        "sh       %[Temp1],             8(%[output])                    \n\t"

        "sub      %[Temp2],             %[step_1],      %[step_2]       \n\t"
        "sh       %[Temp2],             16(%[output])                   \n\t"

        "sub      %[Temp3],             %[step_0],      %[step_3]       \n\t"
        "sh       %[Temp3],             24(%[output])                   \n\t"

      : [Temp0] "=&r" (Temp0), [Temp1] "=&r" (Temp1),
        [Temp2] "=&r" (Temp2), [Temp3] "=&r" (Temp3),
        [step_0] "=&r" (step_0), [step_1] "=&r" (step_1),
        [step_2] "=&r" (step_2), [step_3] "=&r" (step_3),
        [output] "+r" (output)
      : [const_2_power_13] "r" (const_2_power_13),
        [cospi_8_64] "r" (cospi_8_64), [cospi_16_64] "r" (cospi_16_64),
        [cospi_24_64] "r" (cospi_24_64),
        [input] "r" (input)
    );

    input += 4;
    output += 1;
  }
}

static void vp9_idct4_columns_add_blk_dspr2(int16_t *input, uint8_t *dest,
                                               int dest_stride) {
  int16_t   step_0, step_1, step_2, step_3;
  int       Temp0, Temp1, Temp2, Temp3;
  const int const_2_power_13 = 8192;
  int       i;
  uint8_t   *dest_pix;
  uint8_t   *cm = vpx_ff_cropTbl;

  /* prefetch vpx_ff_cropTbl */
  prefetch_load(vpx_ff_cropTbl);
  prefetch_load(vpx_ff_cropTbl +  32);
  prefetch_load(vpx_ff_cropTbl +  64);
  prefetch_load(vpx_ff_cropTbl +  96);
  prefetch_load(vpx_ff_cropTbl + 128);
  prefetch_load(vpx_ff_cropTbl + 160);
  prefetch_load(vpx_ff_cropTbl + 192);
  prefetch_load(vpx_ff_cropTbl + 224);

  for (i = 0; i < 4; ++i) {
      dest_pix = (dest + i);

    __asm__ __volatile__ (
        /*
          temp_1 = (input[0] + input[2]) * cospi_16_64;
          step_0 = dct_const_round_shift(temp_1);

          temp_2 = (input[0] - input[2]) * cospi_16_64;
          step_1 = dct_const_round_shift(temp_2);
        */
        "lh       %[Temp0],             0(%[input])                     \n\t"
        "lh       %[Temp1],             4(%[input])                     \n\t"
        "mtlo     %[const_2_power_13],  $ac0                            \n\t"
        "mthi     $zero,                $ac0                            \n\t"
        "mtlo     %[const_2_power_13],  $ac1                            \n\t"
        "mthi     $zero,                $ac1                            \n\t"
        "add      %[Temp2],             %[Temp0],       %[Temp1]        \n\t"
        "sub      %[Temp3],             %[Temp0],       %[Temp1]        \n\t"
        "madd     $ac0,                 %[Temp2],       %[cospi_16_64]  \n\t"
        "lh       %[Temp0],             2(%[input])                     \n\t"
        "lh       %[Temp1],             6(%[input])                     \n\t"
        "extp     %[step_0],            $ac0,           31              \n\t"
        "mtlo     %[const_2_power_13],  $ac0                            \n\t"
        "mthi     $zero,                $ac0                            \n\t"

        "madd     $ac1,                 %[Temp3],       %[cospi_16_64]  \n\t"
        "extp     %[step_1],            $ac1,           31              \n\t"
        "mtlo     %[const_2_power_13],  $ac1                            \n\t"
        "mthi     $zero,                $ac1                            \n\t"

        /*
          temp1 = input[1] * cospi_24_64 - input[3] * cospi_8_64;
          step_2 = dct_const_round_shift(temp1);
        */
        "madd     $ac0,                 %[Temp0],       %[cospi_24_64]  \n\t"
        "msub     $ac0,                 %[Temp1],       %[cospi_8_64]   \n\t"
        "extp     %[step_2],            $ac0,           31              \n\t"

        /*
          temp2 = input[1] * cospi_8_64 + input[3] * cospi_24_64;
          step_3 = dct_const_round_shift(temp2);
        */
        "madd     $ac1,                 %[Temp0],       %[cospi_8_64]   \n\t"
        "madd     $ac1,                 %[Temp1],       %[cospi_24_64]  \n\t"
        "extp     %[step_3],            $ac1,           31              \n\t"

        /*
          output[0]  = step_0 + step_3;
          output[4]  = step_1 + step_2;
          output[8]  = step_1 - step_2;
          output[12] = step_0 - step_3;
        */
        "add      %[Temp0],             %[step_0],      %[step_3]       \n\t"
        "addi     %[Temp0],             %[Temp0],       8               \n\t"
        "sra      %[Temp0],             %[Temp0],       4               \n\t"
        "lbu      %[Temp1],             0(%[dest_pix])                  \n\t"
        "add      %[Temp1],             %[Temp1],       %[Temp0]        \n\t"
        "add      %[Temp0],             %[step_1],      %[step_2]       \n\t"
        "lbux     %[Temp2],             %[Temp1](%[cm])                 \n\t"
        "sb       %[Temp2],             0(%[dest_pix])                  \n\t"
        "addu     %[dest_pix],          %[dest_pix],    %[dest_stride]  \n\t"

        "addi     %[Temp0],             %[Temp0],       8               \n\t"
        "sra      %[Temp0],             %[Temp0],       4               \n\t"
        "lbu      %[Temp1],             0(%[dest_pix])                  \n\t"
        "add      %[Temp1],             %[Temp1],       %[Temp0]        \n\t"
        "sub      %[Temp0],             %[step_1],      %[step_2]       \n\t"
        "lbux     %[Temp2],             %[Temp1](%[cm])                 \n\t"
        "sb       %[Temp2],             0(%[dest_pix])                  \n\t"
        "addu     %[dest_pix],          %[dest_pix],    %[dest_stride]  \n\t"

        "addi     %[Temp0],             %[Temp0],       8               \n\t"
        "sra      %[Temp0],             %[Temp0],       4               \n\t"
        "lbu      %[Temp1],             0(%[dest_pix])                  \n\t"
        "add      %[Temp1],             %[Temp1],       %[Temp0]        \n\t"
        "sub      %[Temp0],             %[step_0],      %[step_3]       \n\t"
        "lbux     %[Temp2],             %[Temp1](%[cm])                 \n\t"
        "sb       %[Temp2],             0(%[dest_pix])                  \n\t"
        "addu     %[dest_pix],          %[dest_pix],    %[dest_stride]  \n\t"

        "addi     %[Temp0],             %[Temp0],       8               \n\t"
        "sra      %[Temp0],             %[Temp0],       4               \n\t"
        "lbu      %[Temp1],             0(%[dest_pix])                  \n\t"
        "add      %[Temp1],             %[Temp1],       %[Temp0]        \n\t"
        "lbux     %[Temp2],             %[Temp1](%[cm])                 \n\t"
        "sb       %[Temp2],             0(%[dest_pix])                  \n\t"

      : [Temp0] "=&r" (Temp0), [Temp1] "=&r" (Temp1),
        [Temp2] "=&r" (Temp2), [Temp3] "=&r" (Temp3),
        [step_0] "=&r" (step_0), [step_1] "=&r" (step_1),
        [step_2] "=&r" (step_2), [step_3] "=&r" (step_3),
        [dest_pix] "+r" (dest_pix)
      : [const_2_power_13] "r" (const_2_power_13),
        [cospi_8_64] "r" (cospi_8_64), [cospi_16_64] "r" (cospi_16_64),
        [cospi_24_64] "r" (cospi_24_64),
        [input] "r" (input), [cm] "r" (cm), [dest_stride] "r" (dest_stride)
    );

    input += 4;
  }
}

void vp9_idct4x4_16_add_dspr2(const int16_t *input, uint8_t *dest,
                              int dest_stride) {
  DECLARE_ALIGNED(32, int16_t, out[4 * 4]);
  int16_t *outptr = out;
  uint32_t pos = 45;

  /* bit positon for extract from acc */
  __asm__ __volatile__ (
    "wrdsp      %[pos],     1           \n\t"
    :
    : [pos] "r" (pos)
  );

  // Rows
  vp9_idct4_rows_dspr2(input, outptr);

  // Columns
  vp9_idct4_columns_add_blk_dspr2(&out[0], dest, dest_stride);
}

void vp9_idct4x4_1_add_dspr2(const int16_t *input, uint8_t *dest,
                             int dest_stride) {
  int       a1, absa1;
  int       r;
  int32_t   out;
  int       t2, vector_a1, vector_a;
  uint32_t  pos = 45;
  int16_t   input_dc = input[0];

  /* bit positon for extract from acc */
  __asm__ __volatile__ (
    "wrdsp      %[pos],     1           \n\t"

    :
    : [pos] "r" (pos)
  );

  out = DCT_CONST_ROUND_SHIFT_TWICE_COSPI_16_64(input_dc);
  __asm__ __volatile__ (
      "addi     %[out],     %[out],    8       \n\t"
      "sra      %[a1],      %[out],    4       \n\t"

      : [out] "+r" (out), [a1] "=r" (a1)
      :
  );

  if (a1 < 0) {
    /* use quad-byte
     * input and output memory are four byte aligned */
    __asm__ __volatile__ (
        "abs        %[absa1],     %[a1]         \n\t"
        "replv.qb   %[vector_a1], %[absa1]      \n\t"

        : [absa1] "=r" (absa1), [vector_a1] "=r" (vector_a1)
        : [a1] "r" (a1)
    );

    for (r = 4; r--;) {
      __asm__ __volatile__ (
          "lw             %[t2],          0(%[dest])                      \n\t"
          "subu_s.qb      %[vector_a],    %[t2],          %[vector_a1]    \n\t"
          "sw             %[vector_a],    0(%[dest])                      \n\t"
          "add            %[dest],        %[dest],        %[dest_stride]  \n\t"

          : [t2] "=&r" (t2), [vector_a] "=&r" (vector_a),
            [dest] "+&r" (dest)
          : [dest_stride] "r" (dest_stride), [vector_a1] "r" (vector_a1)
      );
    }
  } else {
    /* use quad-byte
     * input and output memory are four byte aligned */
    __asm__ __volatile__ (
        "replv.qb       %[vector_a1],   %[a1]     \n\t"
        : [vector_a1] "=r" (vector_a1)
        : [a1] "r" (a1)
    );

    for (r = 4; r--;) {
      __asm__ __volatile__ (
          "lw           %[t2],          0(%[dest])                        \n\t"
          "addu_s.qb    %[vector_a],    %[t2],            %[vector_a1]    \n\t"
          "sw           %[vector_a],    0(%[dest])                        \n\t"
          "add          %[dest],        %[dest],          %[dest_stride]  \n\t"

          : [t2] "=&r" (t2), [vector_a] "=&r" (vector_a),
            [dest] "+&r" (dest)
          : [dest_stride] "r" (dest_stride), [vector_a1] "r" (vector_a1)
      );
    }
  }
}

static void iadst4_dspr2(const int16_t *input, int16_t *output) {
  int s0, s1, s2, s3, s4, s5, s6, s7;
  int x0, x1, x2, x3;

  x0 = input[0];
  x1 = input[1];
  x2 = input[2];
  x3 = input[3];

  if (!(x0 | x1 | x2 | x3)) {
    output[0] = output[1] = output[2] = output[3] = 0;
    return;
  }

  s0 = sinpi_1_9 * x0;
  s1 = sinpi_2_9 * x0;
  s2 = sinpi_3_9 * x1;
  s3 = sinpi_4_9 * x2;
  s4 = sinpi_1_9 * x2;
  s5 = sinpi_2_9 * x3;
  s6 = sinpi_4_9 * x3;
  s7 = x0 - x2 + x3;

  x0 = s0 + s3 + s5;
  x1 = s1 - s4 - s6;
  x2 = sinpi_3_9 * s7;
  x3 = s2;

  s0 = x0 + x3;
  s1 = x1 + x3;
  s2 = x2;
  s3 = x0 + x1 - x3;

  // 1-D transform scaling factor is sqrt(2).
  // The overall dynamic range is 14b (input) + 14b (multiplication scaling)
  // + 1b (addition) = 29b.
  // Hence the output bit depth is 15b.
  output[0] = dct_const_round_shift(s0);
  output[1] = dct_const_round_shift(s1);
  output[2] = dct_const_round_shift(s2);
  output[3] = dct_const_round_shift(s3);
}

void vp9_iht4x4_16_add_dspr2(const int16_t *input, uint8_t *dest,
                             int dest_stride, int tx_type) {
  int i, j;
  DECLARE_ALIGNED(32, int16_t, out[4 * 4]);
  int16_t *outptr = out;
  int16_t temp_in[4 * 4], temp_out[4];
  uint32_t pos = 45;

  /* bit positon for extract from acc */
  __asm__ __volatile__ (
    "wrdsp      %[pos],     1           \n\t"
    :
    : [pos] "r" (pos)
  );

  switch (tx_type) {
    case DCT_DCT:   // DCT in both horizontal and vertical
      vp9_idct4_rows_dspr2(input, outptr);
      vp9_idct4_columns_add_blk_dspr2(&out[0], dest, dest_stride);
      break;
    case ADST_DCT:  // ADST in vertical, DCT in horizontal
      vp9_idct4_rows_dspr2(input, outptr);

      outptr = out;

      for (i = 0; i < 4; ++i) {
        iadst4_dspr2(outptr, temp_out);

        for (j = 0; j < 4; ++j)
          dest[j * dest_stride + i] =
                    clip_pixel(ROUND_POWER_OF_TWO(temp_out[j], 4)
                                      + dest[j * dest_stride + i]);

        outptr += 4;
      }
      break;
    case DCT_ADST:  // DCT in vertical, ADST in horizontal
      for (i = 0; i < 4; ++i) {
        iadst4_dspr2(input, outptr);
        input  += 4;
        outptr += 4;
      }

      for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j) {
          temp_in[i * 4 + j] = out[j * 4 + i];
        }
      }
      vp9_idct4_columns_add_blk_dspr2(&temp_in[0], dest, dest_stride);
      break;
    case ADST_ADST:  // ADST in both directions
      for (i = 0; i < 4; ++i) {
        iadst4_dspr2(input, outptr);
        input  += 4;
        outptr += 4;
      }

      for (i = 0; i < 4; ++i) {
        for (j = 0; j < 4; ++j)
          temp_in[j] = out[j * 4 + i];
        iadst4_dspr2(temp_in, temp_out);

        for (j = 0; j < 4; ++j)
          dest[j * dest_stride + i] =
                  clip_pixel(ROUND_POWER_OF_TWO(temp_out[j], 4)
                                      + dest[j * dest_stride + i]);
      }
      break;
    default:
      printf("vp9_short_iht4x4_add_dspr2 : Invalid tx_type\n");
      break;
  }
}
#endif  // #if HAVE_DSPR2
