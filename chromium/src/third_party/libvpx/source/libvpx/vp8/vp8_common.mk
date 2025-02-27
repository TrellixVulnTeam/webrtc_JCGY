##
##  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
##
##  Use of this source code is governed by a BSD-style license
##  that can be found in the LICENSE file in the root of the source
##  tree. An additional intellectual property rights grant can be found
##  in the file PATENTS.  All contributing project authors may
##  be found in the AUTHORS file in the root of the source tree.
##

VP8_COMMON_SRCS-yes += vp8_common.mk
VP8_COMMON_SRCS-yes += common/ppflags.h
VP8_COMMON_SRCS-yes += common/onyx.h
VP8_COMMON_SRCS-yes += common/onyxd.h
VP8_COMMON_SRCS-yes += common/alloccommon.c
VP8_COMMON_SRCS-yes += common/blockd.c
VP8_COMMON_SRCS-yes += common/coefupdateprobs.h
VP8_COMMON_SRCS-yes += common/copy_c.c
VP8_COMMON_SRCS-yes += common/debugmodes.c
VP8_COMMON_SRCS-yes += common/default_coef_probs.h
VP8_COMMON_SRCS-yes += common/dequantize.c
VP8_COMMON_SRCS-yes += common/entropy.c
VP8_COMMON_SRCS-yes += common/entropymode.c
VP8_COMMON_SRCS-yes += common/entropymv.c
VP8_COMMON_SRCS-yes += common/extend.c
VP8_COMMON_SRCS-yes += common/filter.c
VP8_COMMON_SRCS-yes += common/filter.h
VP8_COMMON_SRCS-yes += common/findnearmv.c
VP8_COMMON_SRCS-yes += common/generic/systemdependent.c
VP8_COMMON_SRCS-yes += common/idct_blk.c
VP8_COMMON_SRCS-yes += common/idctllm.c
VP8_COMMON_SRCS-yes += common/alloccommon.h
VP8_COMMON_SRCS-yes += common/blockd.h
VP8_COMMON_SRCS-yes += common/common.h
VP8_COMMON_SRCS-yes += common/entropy.h
VP8_COMMON_SRCS-yes += common/entropymode.h
VP8_COMMON_SRCS-yes += common/entropymv.h
VP8_COMMON_SRCS-yes += common/extend.h
VP8_COMMON_SRCS-yes += common/findnearmv.h
VP8_COMMON_SRCS-yes += common/header.h
VP8_COMMON_SRCS-yes += common/invtrans.h
VP8_COMMON_SRCS-yes += common/loopfilter.h
VP8_COMMON_SRCS-yes += common/modecont.h
VP8_COMMON_SRCS-yes += common/mv.h
VP8_COMMON_SRCS-yes += common/onyxc_int.h
VP8_COMMON_SRCS-yes += common/quant_common.h
VP8_COMMON_SRCS-yes += common/reconinter.h
VP8_COMMON_SRCS-yes += common/reconintra4x4.h
VP8_COMMON_SRCS-yes += common/rtcd.c
VP8_COMMON_SRCS-yes += common/rtcd_defs.pl
VP8_COMMON_SRCS-yes += common/setupintrarecon.h
VP8_COMMON_SRCS-yes += common/swapyv12buffer.h
VP8_COMMON_SRCS-yes += common/systemdependent.h
VP8_COMMON_SRCS-yes += common/threading.h
VP8_COMMON_SRCS-yes += common/treecoder.h
VP8_COMMON_SRCS-yes += common/vp8_loopfilter.c
VP8_COMMON_SRCS-yes += common/loopfilter_filters.c
VP8_COMMON_SRCS-yes += common/mbpitch.c
VP8_COMMON_SRCS-yes += common/modecont.c
VP8_COMMON_SRCS-yes += common/quant_common.c
VP8_COMMON_SRCS-yes += common/reconinter.c
VP8_COMMON_SRCS-yes += common/reconintra.c
VP8_COMMON_SRCS-yes += common/reconintra4x4.c
VP8_COMMON_SRCS-yes += common/setupintrarecon.c
VP8_COMMON_SRCS-yes += common/swapyv12buffer.c
VP8_COMMON_SRCS-yes += common/vp8_entropymodedata.h



VP8_COMMON_SRCS-$(CONFIG_POSTPROC_VISUALIZER) += common/textblit.c
VP8_COMMON_SRCS-yes += common/treecoder.c

VP8_COMMON_SRCS-$(ARCH_X86)$(ARCH_X86_64) += common/x86/filter_x86.c
VP8_COMMON_SRCS-$(ARCH_X86)$(ARCH_X86_64) += common/x86/filter_x86.h
VP8_COMMON_SRCS-$(ARCH_X86)$(ARCH_X86_64) += common/x86/vp8_asm_stubs.c
VP8_COMMON_SRCS-$(ARCH_X86)$(ARCH_X86_64) += common/x86/loopfilter_x86.c
VP8_COMMON_SRCS-$(CONFIG_POSTPROC) += common/mfqe.c
VP8_COMMON_SRCS-$(CONFIG_POSTPROC) += common/postproc.h
VP8_COMMON_SRCS-$(CONFIG_POSTPROC) += common/postproc.c
VP8_COMMON_SRCS-$(HAVE_MMX) += common/x86/dequantize_mmx.asm
VP8_COMMON_SRCS-$(HAVE_MMX) += common/x86/idct_blk_mmx.c
VP8_COMMON_SRCS-$(HAVE_MMX) += common/x86/idctllm_mmx.asm
VP8_COMMON_SRCS-$(HAVE_MMX) += common/x86/iwalsh_mmx.asm
VP8_COMMON_SRCS-$(HAVE_MMX) += common/x86/vp8_loopfilter_mmx.asm
VP8_COMMON_SRCS-$(HAVE_MMX) += common/x86/recon_mmx.asm
VP8_COMMON_SRCS-$(HAVE_MMX) += common/x86/subpixel_mmx.asm
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/copy_sse2.asm
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/idct_blk_sse2.c
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/idctllm_sse2.asm
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/recon_sse2.asm
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/recon_wrapper_sse2.c
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/subpixel_sse2.asm
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/loopfilter_sse2.asm
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/iwalsh_sse2.asm
VP8_COMMON_SRCS-$(HAVE_SSE3) += common/x86/copy_sse3.asm
VP8_COMMON_SRCS-$(HAVE_SSSE3) += common/x86/subpixel_ssse3.asm

ifeq ($(CONFIG_POSTPROC),yes)
VP8_COMMON_SRCS-$(HAVE_MMX) += common/x86/postproc_mmx.asm
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/mfqe_sse2.asm
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/postproc_sse2.asm
endif

ifeq ($(ARCH_X86_64),yes)
VP8_COMMON_SRCS-$(HAVE_SSE2) += common/x86/loopfilter_block_sse2_x86_64.asm
endif

# common (c)
VP8_COMMON_SRCS-$(HAVE_DSPR2)  += common/mips/dspr2/idctllm_dspr2.c
VP8_COMMON_SRCS-$(HAVE_DSPR2)  += common/mips/dspr2/filter_dspr2.c
VP8_COMMON_SRCS-$(HAVE_DSPR2)  += common/mips/dspr2/loopfilter_filters_dspr2.c
VP8_COMMON_SRCS-$(HAVE_DSPR2)  += common/mips/dspr2/reconinter_dspr2.c
VP8_COMMON_SRCS-$(HAVE_DSPR2)  += common/mips/dspr2/idct_blk_dspr2.c
VP8_COMMON_SRCS-$(HAVE_DSPR2)  += common/mips/dspr2/dequantize_dspr2.c

# common (c)
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/bilinear_filter_msa.c
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/copymem_msa.c
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/idct_msa.c
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/loopfilter_filters_msa.c
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/reconintra_msa.c
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/sixtap_filter_msa.c
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/vp8_macros_msa.h

ifeq ($(CONFIG_POSTPROC),yes)
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/mfqe_msa.c
VP8_COMMON_SRCS-$(HAVE_MSA) += common/mips/msa/postproc_msa.c
endif

# common (c)
VP8_COMMON_SRCS-$(ARCH_ARM)  += common/arm/filter_arm.c
VP8_COMMON_SRCS-$(ARCH_ARM)  += common/arm/loopfilter_arm.c
VP8_COMMON_SRCS-$(ARCH_ARM)  += common/arm/dequantize_arm.c

# common (media)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/bilinearfilter_arm.c
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/bilinearfilter_arm.h
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/bilinearfilter_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/copymem8x4_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/copymem8x8_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/copymem16x16_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/dc_only_idct_add_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/iwalsh_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/filter_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/idct_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/loopfilter_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/simpleloopfilter_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/sixtappredict8x4_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/intra4x4_predict_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/dequant_idct_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/dequantize_v6$(ASM)
VP8_COMMON_SRCS-$(HAVE_MEDIA)  += common/arm/armv6/idct_blk_v6.c

# common (neon intrinsics)
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/bilinearpredict_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/copymem_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/dc_only_idct_add_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/dequant_idct_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/dequantizeb_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/idct_blk_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/idct_dequant_0_2x_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/idct_dequant_full_2x_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/iwalsh_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/vp8_loopfilter_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/loopfiltersimplehorizontaledge_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/loopfiltersimpleverticaledge_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/mbloopfilter_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/reconintra_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/shortidct4x4llm_neon.c
VP8_COMMON_SRCS-$(HAVE_NEON)  += common/arm/neon/sixtappredict_neon.c

$(eval $(call rtcd_h_template,vp8_rtcd,vp8/common/rtcd_defs.pl))
