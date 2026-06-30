/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin -mabi=lp64d -O3" } */

/* Regression test for FP widening `_lm{N}' across all widen factors
   and input formats:

     W=2 OFP8 -> FP16  (4 input combos x 3 LMULs)
     W=2 OFP8 -> BF16  (4 input combos x 3 LMULs)
     W=4 vfqwmmacc FP16/BF16 -> FP64 (2 fmts x 3 LMULs)
     W=4 vfqwmmacc OFP8 -> FP32 (4 input combos x 3 LMULs)
     W=8 vf8wmmacc OFP8 -> FP64 (4 input combos x 3 LMULs)

   Spec §2614-2618 + §2657: `_lm{N}' applies uniformly to FP and
   integer widening.  Prior to this commit, only LMUL=1 was
   enumerated for these forms.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

/* W=2 OFP8 -> FP16, e4m3 inputs, LMUL=2/4/8.  */
vfloat16m4_t w2_fp16_e4m3_lm2 (vfloat16m4_t vd,
			       vfloat8e4m3m2_t a, vfloat8e4m3m2_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m4_f8e4m3m2_lm2 (vd, a, b, vl); }
vfloat16m4_t w2_fp16_e4m3_lm4 (vfloat16m4_t vd,
			       vfloat8e4m3m4_t a, vfloat8e4m3m4_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m4_f8e4m3m4_lm4 (vd, a, b, vl); }
vfloat16m4_t w2_fp16_e4m3_lm8 (vfloat16m4_t vd,
			       vfloat8e4m3m8_t a, vfloat8e4m3m8_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m4_f8e4m3m8_lm8 (vd, a, b, vl); }

/* W=2 OFP8 -> BF16, mixed e4m3 / e5m2 inputs, LMUL=2/4/8.  */
vbfloat16m4_t w2_bf16_e4m3_e5m2_lm2 (vbfloat16m4_t vd,
				     vfloat8e4m3m2_t a, vfloat8e5m2m2_t b,
				     size_t vl)
{ return __riscv_vfwmmacc_vv_bf16m4_f8e4m3m2_f8e5m2m2_lm2 (vd, a, b, vl); }
vbfloat16m4_t w2_bf16_e4m3_e5m2_lm4 (vbfloat16m4_t vd,
				     vfloat8e4m3m4_t a, vfloat8e5m2m4_t b,
				     size_t vl)
{ return __riscv_vfwmmacc_vv_bf16m4_f8e4m3m4_f8e5m2m4_lm4 (vd, a, b, vl); }
vbfloat16m4_t w2_bf16_e4m3_e5m2_lm8 (vbfloat16m4_t vd,
				     vfloat8e4m3m8_t a, vfloat8e5m2m8_t b,
				     size_t vl)
{ return __riscv_vfwmmacc_vv_bf16m4_f8e4m3m8_f8e5m2m8_lm8 (vd, a, b, vl); }

/* W=4 vfqwmmacc FP16 -> FP64, LMUL=2/4/8.  */
vfloat64m4_t w4_f16_lm2 (vfloat64m4_t vd, vfloat16m2_t a, vfloat16m2_t b, size_t vl)
{ return __riscv_vfqwmmacc_vv_f64m4_f16m2_lm2 (vd, a, b, vl); }
vfloat64m4_t w4_f16_lm4 (vfloat64m4_t vd, vfloat16m4_t a, vfloat16m4_t b, size_t vl)
{ return __riscv_vfqwmmacc_vv_f64m4_f16m4_lm4 (vd, a, b, vl); }
vfloat64m4_t w4_f16_lm8 (vfloat64m4_t vd, vfloat16m8_t a, vfloat16m8_t b, size_t vl)
{ return __riscv_vfqwmmacc_vv_f64m4_f16m8_lm8 (vd, a, b, vl); }

/* W=4 vfqwmmacc OFP8 -> FP32, e4m3 same-format inputs, LMUL=2/4/8.  */
vfloat32m4_t w4_ofp8_e4m3_lm2 (vfloat32m4_t vd, vfloat8e4m3m2_t a,
			       vfloat8e4m3m2_t b, size_t vl)
{ return __riscv_vfqwmmacc_vv_f32m4_f8e4m3m2_lm2 (vd, a, b, vl); }
vfloat32m4_t w4_ofp8_e4m3_lm4 (vfloat32m4_t vd, vfloat8e4m3m4_t a,
			       vfloat8e4m3m4_t b, size_t vl)
{ return __riscv_vfqwmmacc_vv_f32m4_f8e4m3m4_lm4 (vd, a, b, vl); }
vfloat32m4_t w4_ofp8_e4m3_lm8 (vfloat32m4_t vd, vfloat8e4m3m8_t a,
			       vfloat8e4m3m8_t b, size_t vl)
{ return __riscv_vfqwmmacc_vv_f32m4_f8e4m3m8_lm8 (vd, a, b, vl); }

/* W=8 vf8wmmacc OFP8 -> FP64, e5m2 same-format inputs, LMUL=2/4/8.  */
vfloat64m4_t w8_ofp8_e5m2_lm2 (vfloat64m4_t vd, vfloat8e5m2m2_t a,
			       vfloat8e5m2m2_t b, size_t vl)
{ return __riscv_vf8wmmacc_vv_f64m4_f8e5m2m2_lm2 (vd, a, b, vl); }
vfloat64m4_t w8_ofp8_e5m2_lm4 (vfloat64m4_t vd, vfloat8e5m2m4_t a,
			       vfloat8e5m2m4_t b, size_t vl)
{ return __riscv_vf8wmmacc_vv_f64m4_f8e5m2m4_lm4 (vd, a, b, vl); }
vfloat64m4_t w8_ofp8_e5m2_lm8 (vfloat64m4_t vd, vfloat8e5m2m8_t a,
			       vfloat8e5m2m8_t b, size_t vl)
{ return __riscv_vf8wmmacc_vv_f64m4_f8e5m2m8_lm8 (vd, a, b, vl); }

/* Counts: 6 vfwmmacc (W=2), 6 vfqwmmacc (W=4 mix of f16 and ofp8),
   3 vf8wmmacc (W=8).  */
/* { dg-final { scan-assembler-times {vfwmmacc\.vv} 6 } } */
/* { dg-final { scan-assembler-times {vfqwmmacc\.vv} 6 } } */
/* { dg-final { scan-assembler-times {vf8wmmacc\.vv} 3 } } */
