/*
 *  Copyright 2015 The LibYuv Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS. All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdlib.h>

#include "libyuv/convert.h"
#include "libyuv/convert_argb.h"
#include "libyuv/convert_from.h"
#include "libyuv/convert_from_argb.h"
#include "libyuv/cpu_id.h"
#include "libyuv/row.h"  // For Sobel
#include "../unit_test/unit_test.h"

namespace libyuv {

// TODO(fbarchard): Port high accuracy YUV to RGB to Neon.
#if !defined(LIBYUV_DISABLE_NEON) && \
    (defined(__aarch64__) || defined(__ARM_NEON__) || defined(LIBYUV_NEON))
#define ERROR_R 1
#define ERROR_G 1
#define ERROR_B 3
#define ERROR_FULL 6
#define ERROR_J420 4
#else
#define ERROR_R 1
#define ERROR_G 1
#define ERROR_B 3
#define ERROR_FULL 5
#define ERROR_J420 3
#endif

#define TESTCS(TESTNAME, YUVTOARGB, ARGBTOYUV, HS1, HS, HN, DIFF)              \
TEST_F(libyuvTest, TESTNAME) {                                                 \
  const int kPixels = benchmark_width_ * benchmark_height_;                    \
  const int kHalfPixels = ((benchmark_width_ + 1) / 2) *                       \
      ((benchmark_height_ + HS1) / HS);                                        \
  align_buffer_64(orig_y, kPixels);                                            \
  align_buffer_64(orig_u, kHalfPixels);                                        \
  align_buffer_64(orig_v, kHalfPixels);                                        \
  align_buffer_64(orig_pixels, kPixels * 4);                                   \
  align_buffer_64(temp_y, kPixels);                                            \
  align_buffer_64(temp_u, kHalfPixels);                                        \
  align_buffer_64(temp_v, kHalfPixels);                                        \
  align_buffer_64(dst_pixels_opt, kPixels * 4);                                \
  align_buffer_64(dst_pixels_c, kPixels * 4);                                  \
                                                                               \
  MemRandomize(orig_pixels, kPixels * 4);                                      \
  MemRandomize(orig_y, kPixels);                                               \
  MemRandomize(orig_u, kHalfPixels);                                           \
  MemRandomize(orig_v, kHalfPixels);                                           \
  MemRandomize(temp_y, kPixels);                                               \
  MemRandomize(temp_u, kHalfPixels);                                           \
  MemRandomize(temp_v, kHalfPixels);                                           \
  MemRandomize(dst_pixels_opt, kPixels * 4);                                   \
  MemRandomize(dst_pixels_c, kPixels * 4);                                     \
                                                                               \
  /* The test is overall for color conversion matrix being reversible, so */   \
  /* this initializes the pixel with 2x2 blocks to eliminate subsampling. */   \
  uint8* p = orig_y;                                                           \
  for (int y = 0; y < benchmark_height_ - HS1; y += HS) {                      \
    for (int x = 0; x < benchmark_width_ - 1; x += 2) {                        \
      uint8 r = static_cast<uint8>(random());                                  \
      p[0] = r;                                                                \
      p[1] = r;                                                                \
      p[HN] = r;                                                               \
      p[HN + 1] = r;                                                           \
      p += 2;                                                                  \
    }                                                                          \
    if (benchmark_width_ & 1) {                                                \
      uint8 r = static_cast<uint8>(random());                                  \
      p[0] = r;                                                                \
      p[HN] = r;                                                               \
      p += 1;                                                                  \
    }                                                                          \
    p += HN;                                                                   \
  }                                                                            \
                                                                               \
  /* Start with YUV converted to ARGB. */                                      \
  YUVTOARGB(orig_y, benchmark_width_,                                          \
            orig_u, (benchmark_width_ + 1) / 2,                                \
            orig_v, (benchmark_width_ + 1) / 2,                                \
            orig_pixels, benchmark_width_ * 4,                                 \
            benchmark_width_, benchmark_height_);                              \
                                                                               \
  ARGBTOYUV(orig_pixels, benchmark_width_ * 4,                                 \
            temp_y, benchmark_width_,                                          \
            temp_u, (benchmark_width_ + 1) / 2,                                \
            temp_v, (benchmark_width_ + 1) / 2,                                \
            benchmark_width_, benchmark_height_);                              \
                                                                               \
  MaskCpuFlags(disable_cpu_flags_);                                            \
  YUVTOARGB(temp_y, benchmark_width_,                                          \
            temp_u, (benchmark_width_ + 1) / 2,                                \
            temp_v, (benchmark_width_ + 1) / 2,                                \
            dst_pixels_c, benchmark_width_ * 4,                                \
            benchmark_width_, benchmark_height_);                              \
  MaskCpuFlags(-1);                                                            \
                                                                               \
  for (int i = 0; i < benchmark_iterations_; ++i) {                            \
    YUVTOARGB(temp_y, benchmark_width_,                                        \
              temp_u, (benchmark_width_ + 1) / 2,                              \
              temp_v, (benchmark_width_ + 1) / 2,                              \
              dst_pixels_opt, benchmark_width_ * 4,                            \
              benchmark_width_, benchmark_height_);                            \
  }                                                                            \
  /* Test C and SIMD match. */                                                 \
  for (int i = 0; i < kPixels * 4; ++i) {                                      \
    EXPECT_EQ(dst_pixels_c[i], dst_pixels_opt[i]);                             \
  }                                                                            \
  /* Test SIMD is close to original. */                                        \
  for (int i = 0; i < kPixels * 4; ++i) {                                      \
    EXPECT_NEAR(static_cast<int>(orig_pixels[i]),                              \
                static_cast<int>(dst_pixels_opt[i]), DIFF);                    \
  }                                                                            \
                                                                               \
  free_aligned_buffer_64(orig_pixels);                                         \
  free_aligned_buffer_64(orig_y);                                              \
  free_aligned_buffer_64(orig_u);                                              \
  free_aligned_buffer_64(orig_v);                                              \
  free_aligned_buffer_64(temp_y);                                              \
  free_aligned_buffer_64(temp_u);                                              \
  free_aligned_buffer_64(temp_v);                                              \
  free_aligned_buffer_64(dst_pixels_opt);                                      \
  free_aligned_buffer_64(dst_pixels_c);                                        \
}                                                                              \

TESTCS(ScaleTestI420, I420ToARGB, ARGBToI420, 1, 2, benchmark_width_,
       ERROR_FULL)
TESTCS(ScaleTestI422, I422ToARGB, ARGBToI422, 0, 1, 0, ERROR_FULL)
TESTCS(ScaleTestJ420, J420ToARGB, ARGBToJ420, 1, 2, benchmark_width_,
       ERROR_J420)
TESTCS(ScaleTestJ422, J422ToARGB, ARGBToJ422, 0, 1, 0, 3)

static void YUVToRGB(int y, int u, int v, int* r, int* g, int* b) {
  const int kWidth = 16;
  const int kHeight = 1;
  const int kPixels = kWidth * kHeight;
  const int kHalfPixels = ((kWidth + 1) / 2) * ((kHeight + 1) / 2);

  SIMD_ALIGNED(uint8 orig_y[16]);
  SIMD_ALIGNED(uint8 orig_u[8]);
  SIMD_ALIGNED(uint8 orig_v[8]);
  SIMD_ALIGNED(uint8 orig_pixels[16 * 4]);
  memset(orig_y, y, kPixels);
  memset(orig_u, u, kHalfPixels);
  memset(orig_v, v, kHalfPixels);

  /* YUV converted to ARGB. */
  I422ToARGB(orig_y, kWidth,
             orig_u, (kWidth + 1) / 2,
             orig_v, (kWidth + 1) / 2,
             orig_pixels, kWidth * 4,
             kWidth, kHeight);

  *b = orig_pixels[0];
  *g = orig_pixels[1];
  *r = orig_pixels[2];
}

static void YUVJToRGB(int y, int u, int v, int* r, int* g, int* b) {
  const int kWidth = 16;
  const int kHeight = 1;
  const int kPixels = kWidth * kHeight;
  const int kHalfPixels = ((kWidth + 1) / 2) * ((kHeight + 1) / 2);

  SIMD_ALIGNED(uint8 orig_y[16]);
  SIMD_ALIGNED(uint8 orig_u[8]);
  SIMD_ALIGNED(uint8 orig_v[8]);
  SIMD_ALIGNED(uint8 orig_pixels[16 * 4]);
  memset(orig_y, y, kPixels);
  memset(orig_u, u, kHalfPixels);
  memset(orig_v, v, kHalfPixels);

  /* YUV converted to ARGB. */
  J422ToARGB(orig_y, kWidth,
             orig_u, (kWidth + 1) / 2,
             orig_v, (kWidth + 1) / 2,
             orig_pixels, kWidth * 4,
             kWidth, kHeight);

  *b = orig_pixels[0];
  *g = orig_pixels[1];
  *r = orig_pixels[2];
}

static void YToRGB(int y, int* r, int* g, int* b) {
  const int kWidth = 16;
  const int kHeight = 1;
  const int kPixels = kWidth * kHeight;

  SIMD_ALIGNED(uint8 orig_y[16]);
  SIMD_ALIGNED(uint8 orig_pixels[16 * 4]);
  memset(orig_y, y, kPixels);

  /* YUV converted to ARGB. */
  I400ToARGB(orig_y, kWidth, orig_pixels, kWidth * 4, kWidth, kHeight);

  *b = orig_pixels[0];
  *g = orig_pixels[1];
  *r = orig_pixels[2];
}

static void YJToRGB(int y, int* r, int* g, int* b) {
  const int kWidth = 16;
  const int kHeight = 1;
  const int kPixels = kWidth * kHeight;

  SIMD_ALIGNED(uint8 orig_y[16]);
  SIMD_ALIGNED(uint8 orig_pixels[16 * 4]);
  memset(orig_y, y, kPixels);

  /* YUV converted to ARGB. */
  J400ToARGB(orig_y, kWidth, orig_pixels, kWidth * 4, kWidth, kHeight);

  *b = orig_pixels[0];
  *g = orig_pixels[1];
  *r = orig_pixels[2];
}

// Pick a method for clamping.
#define CLAMPMETHOD_IF 1
//  #define CLAMPMETHOD_TABLE 1
//  #define CLAMPMETHOD_TERNARY 1
//  #define CLAMPMETHOD_MASK 1

// Pick a method for rounding.
#define ROUND(f) static_cast<int>(f + 0.5)
//  #define ROUND(f) lrintf(f)
//  #define ROUND(f) static_cast<int>(round(f))
//  #define ROUND(f) _mm_cvt_ss2si(_mm_load_ss(&f))

#if defined(CLAMPMETHOD_IF)
static int RoundToByte(float f) {
  int i =  ROUND(f);
  if (i < 0) {
    i = 0;
  }
  if (i > 255) {
    i = 255;
  }
  return i;
}
#elif defined(CLAMPMETHOD_TABLE)
static const unsigned char clamptable[811] = {
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
  29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
  48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
  67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
  86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103,
  104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118,
  119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
  134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148,
  149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163,
  164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178,
  179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193,
  194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208,
  209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223,
  224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238,
  239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253,
  254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
  255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

static int RoundToByte(float f) {
  return clamptable[ROUND(f) + 276];
}
#elif defined(CLAMPMETHOD_TERNARY)
static int RoundToByte(float f) {
  int i = ROUND(f);
  return (i < 0) ? 0 : ((i > 255) ? 255 : i);
}
#elif defined(CLAMPMETHOD_MASK)
static int RoundToByte(float f) {
  int i = ROUND(f);
  i =  ((-(i) >> 31) & (i));  // clamp to 0.
  return (((255 - (i)) >> 31) | (i)) & 255;  // clamp to 255.
}
#endif

#define RANDOM256(s) ((s & 1) ? ((s >> 1) ^ 0xb8) : (s >> 1))

TEST_F(libyuvTest, ScaleTestRoundToByte) {
  int allb = 0;
  for (int i = 0; i < benchmark_iterations_; ++i) {
    for (int u2 = 0; u2 < 256; ++u2) {
      for (int v2 = 0; v2 < 256; ++v2) {
        for (int y2 = 0; y2 < 256; ++y2) {
          int y = RANDOM256(y2);
          int b = RoundToByte(y * 810.33 - 257);
          allb |= b;
        }
      }
    }
  }
  EXPECT_GE(allb, 0);
  EXPECT_LE(allb, 255);
}

static void YUVToRGBReference(int y, int u, int v, int* r, int* g, int* b) {
  *r = RoundToByte((y - 16) * 1.164 - (v - 128) * -1.596);
  *g = RoundToByte((y - 16) * 1.164 - (u - 128) * 0.391 - (v - 128) * 0.813);
  *b = RoundToByte((y - 16) * 1.164 - (u - 128) * -2.018);
}

static void YUVJToRGBReference(int y, int u, int v, int* r, int* g, int* b) {
  *r = RoundToByte(y - (v - 128) * -1.40200);
  *g = RoundToByte(y - (u - 128) * 0.34414 - (v - 128) * 0.71414);
  *b = RoundToByte(y - (u - 128) * -1.77200);
}

TEST_F(libyuvTest, ScaleTestYUV) {
  int r0, g0, b0, r1, g1, b1;

  // cyan (less red)
  YUVToRGBReference(240, 255, 0, &r0, &g0, &b0);
  EXPECT_EQ(56, r0);
  EXPECT_EQ(255, g0);
  EXPECT_EQ(255, b0);

  YUVToRGB(240, 255, 0, &r1, &g1, &b1);
  EXPECT_EQ(57, r1);
  EXPECT_EQ(255, g1);
  EXPECT_EQ(255, b1);

  // green (less red and blue)
  YUVToRGBReference(240, 0, 0, &r0, &g0, &b0);
  EXPECT_EQ(56, r0);
  EXPECT_EQ(255, g0);
  EXPECT_EQ(2, b0);

  YUVToRGB(240, 0, 0, &r1, &g1, &b1);
  EXPECT_EQ(57, r1);
  EXPECT_EQ(255, g1);
  EXPECT_EQ(5, b1);

  for (int i = 0; i < 256; ++i) {
    YUVToRGBReference(i, 128, 128, &r0, &g0, &b0);
    YUVToRGB(i, 128, 128, &r1, &g1, &b1);
    EXPECT_NEAR(r0, r1, ERROR_R);
    EXPECT_NEAR(g0, g1, ERROR_G);
    EXPECT_NEAR(b0, b1, ERROR_B);

    YUVToRGBReference(i, 0, 0, &r0, &g0, &b0);
    YUVToRGB(i, 0, 0, &r1, &g1, &b1);
    EXPECT_NEAR(r0, r1, ERROR_R);
    EXPECT_NEAR(g0, g1, ERROR_G);
    EXPECT_NEAR(b0, b1, ERROR_B);

    YUVToRGBReference(i, 0, 255, &r0, &g0, &b0);
    YUVToRGB(i, 0, 255, &r1, &g1, &b1);
    EXPECT_NEAR(r0, r1, ERROR_R);
    EXPECT_NEAR(g0, g1, ERROR_G);
    EXPECT_NEAR(b0, b1, ERROR_B);
  }
}

TEST_F(libyuvTest, ScaleTestGreyYUV) {
  int r0, g0, b0, r1, g1, b1, r2, g2, b2;

  // black
  YUVToRGBReference(16, 128, 128, &r0, &g0, &b0);
  EXPECT_EQ(0, r0);
  EXPECT_EQ(0, g0);
  EXPECT_EQ(0, b0);

  YUVToRGB(16, 128, 128, &r1, &g1, &b1);
  EXPECT_EQ(0, r1);
  EXPECT_EQ(0, g1);
  EXPECT_EQ(0, b1);

  // white
  YUVToRGBReference(240, 128, 128, &r0, &g0, &b0);
  EXPECT_EQ(255, r0);
  EXPECT_EQ(255, g0);
  EXPECT_EQ(255, b0);

  YUVToRGB(240, 128, 128, &r1, &g1, &b1);
  EXPECT_EQ(255, r1);
  EXPECT_EQ(255, g1);
  EXPECT_EQ(255, b1);

  // grey
  YUVToRGBReference(128, 128, 128, &r0, &g0, &b0);
  EXPECT_EQ(130, r0);
  EXPECT_EQ(130, g0);
  EXPECT_EQ(130, b0);

  YUVToRGB(128, 128, 128, &r1, &g1, &b1);
  EXPECT_EQ(130, r1);
  EXPECT_EQ(130, g1);
  EXPECT_EQ(130, b1);


  for (int y = 0; y < 256; ++y) {
    YUVToRGBReference(y, 128, 128, &r0, &g0, &b0);
    YUVToRGB(y, 128, 128, &r1, &g1, &b1);
    YToRGB(y, &r2, &g2, &b2);
    EXPECT_EQ(r0, r1);
    EXPECT_EQ(g0, g1);
    EXPECT_EQ(b0, b1);
    EXPECT_EQ(r0, r2);
    EXPECT_EQ(g0, g2);
    EXPECT_EQ(b0, b2);
  }
}

static void ScalePrintHistogram(int rh[256], int gh[256], int bh[256]) {
  int i;
  printf("hist");
  for (i = 0; i < 256; ++i) {
    if (rh[i] || gh[i] || bh[i]) {
      printf("\t%8d", i - 128);
    }
  }
  printf("\nred");
  for (i = 0; i < 256; ++i) {
    if (rh[i] || gh[i] || bh[i]) {
      printf("\t%8d", rh[i]);
    }
  }
  printf("\ngreen");
  for (i = 0; i < 256; ++i) {
    if (rh[i] || gh[i] || bh[i]) {
      printf("\t%8d", gh[i]);
    }
  }
  printf("\nblue");
  for (i = 0; i < 256; ++i) {
    if (rh[i] || gh[i] || bh[i]) {
      printf("\t%8d", bh[i]);
    }
  }
  printf("\n");
}

TEST_F(libyuvTest, ScaleTestFullYUV) {
  int rh[256] = { 0, }, gh[256] = { 0, }, bh[256] = { 0, };
  for (int u = 0; u < 256; ++u) {
    for (int v = 0; v < 256; ++v) {
      for (int y2 = 0; y2 < 256; ++y2) {
        int r0, g0, b0, r1, g1, b1;
        int y = RANDOM256(y2);
        YUVToRGBReference(y, u, v, &r0, &g0, &b0);
        YUVToRGB(y, u, v, &r1, &g1, &b1);
        EXPECT_NEAR(r0, r1, ERROR_R);
        EXPECT_NEAR(g0, g1, ERROR_G);
        EXPECT_NEAR(b0, b1, ERROR_B);
        ++rh[r1 - r0 + 128];
        ++gh[g1 - g0 + 128];
        ++bh[b1 - b0 + 128];
      }
    }
  }
  ScalePrintHistogram(rh, gh, bh);
}

TEST_F(libyuvTest, ScaleTestFullYUVJ) {
  int rh[256] = { 0, }, gh[256] = { 0, }, bh[256] = { 0, };
  for (int u = 0; u < 256; ++u) {
    for (int v = 0; v < 256; ++v) {
      for (int y2 = 0; y2 < 256; ++y2) {
        int r0, g0, b0, r1, g1, b1;
        int y = RANDOM256(y2);
        YUVJToRGBReference(y, u, v, &r0, &g0, &b0);
        YUVJToRGB(y, u, v, &r1, &g1, &b1);
        EXPECT_NEAR(r0, r1, 1);
        EXPECT_NEAR(g0, g1, 1);
        EXPECT_NEAR(b0, b1, 1);
        ++rh[r1 - r0 + 128];
        ++gh[g1 - g0 + 128];
        ++bh[b1 - b0 + 128];
      }
    }
  }
  ScalePrintHistogram(rh, gh, bh);
}

TEST_F(libyuvTest, ScaleTestGreyYUVJ) {
  int r0, g0, b0, r1, g1, b1, r2, g2, b2;

  // black
  YUVJToRGBReference(0, 128, 128, &r0, &g0, &b0);
  EXPECT_EQ(0, r0);
  EXPECT_EQ(0, g0);
  EXPECT_EQ(0, b0);

  YUVJToRGB(0, 128, 128, &r1, &g1, &b1);
  EXPECT_EQ(0, r1);
  EXPECT_EQ(0, g1);
  EXPECT_EQ(0, b1);

  // white
  YUVJToRGBReference(255, 128, 128, &r0, &g0, &b0);
  EXPECT_EQ(255, r0);
  EXPECT_EQ(255, g0);
  EXPECT_EQ(255, b0);

  YUVJToRGB(255, 128, 128, &r1, &g1, &b1);
  EXPECT_EQ(255, r1);
  EXPECT_EQ(255, g1);
  EXPECT_EQ(255, b1);

  // grey
  YUVJToRGBReference(128, 128, 128, &r0, &g0, &b0);
  EXPECT_EQ(128, r0);
  EXPECT_EQ(128, g0);
  EXPECT_EQ(128, b0);

  YUVJToRGB(128, 128, 128, &r1, &g1, &b1);
  EXPECT_EQ(128, r1);
  EXPECT_EQ(128, g1);
  EXPECT_EQ(128, b1);

  for (int y = 0; y < 256; ++y) {
    YUVJToRGBReference(y, 128, 128, &r0, &g0, &b0);
    YUVJToRGB(y, 128, 128, &r1, &g1, &b1);
    YJToRGB(y, &r2, &g2, &b2);
    EXPECT_EQ(r0, r1);
    EXPECT_EQ(g0, g1);
    EXPECT_EQ(b0, b1);
    EXPECT_EQ(r0, r2);
    EXPECT_EQ(g0, g2);
    EXPECT_EQ(b0, b2);
  }
}

}  // namespace libyuv
