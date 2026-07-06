/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin -mabi=lp64d -O3" } */

/* Regression test for Zvvfmm widening m16 accumulator mmacc (#489).
   Exercises the full widening surface (W=2/4/8, non-OFP8 and OFP8)
   at the m16 accumulator across all sub-axes.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

/* W=2 non-OFP8: FP16 -> FP32, BF16 -> FP32, FP32 -> FP64.  */
vfloat32m8_t
w2_f16_m16 (vfloat32m8_t lo, vfloat32m8_t hi,
	    vfloat16m1_t a, vfloat16m1_t b, size_t vl)
{
  vfloat32m16_t acc = __riscv_ime_vpair_f32m16 (lo, hi);
  acc = __riscv_vfwmmacc_vv_f32m16_f16m1 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f32m16 (acc);
}
vfloat32m8_t
w2_bf16_m16_lm4 (vfloat32m8_t lo, vfloat32m8_t hi,
		 vbfloat16m4_t a, vbfloat16m4_t b, size_t vl)
{
  vfloat32m16_t acc = __riscv_ime_vpair_f32m16 (lo, hi);
  acc = __riscv_vfwmmacc_vv_f32m16_bf16m4_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f32m16 (acc);
}
vfloat64m8_t
w2_f32_m16_lm2 (vfloat64m8_t lo, vfloat64m8_t hi,
		vfloat32m2_t a, vfloat32m2_t b, size_t vl)
{
  vfloat64m16_t acc = __riscv_ime_vpair_f64m16 (lo, hi);
  acc = __riscv_vfwmmacc_vv_f64m16_f32m2_lm2 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f64m16 (acc);
}

/* W=2 OFP8 -> FP16 (same-format and mixed-format inputs).  */
vfloat16m8_t
w2_ofp8_fp16_m16 (vfloat16m8_t lo, vfloat16m8_t hi,
		  vfloat8e4m3m1_t a, vfloat8e4m3m1_t b, size_t vl)
{
  vfloat16m16_t acc = __riscv_ime_vpair_f16m16 (lo, hi);
  acc = __riscv_vfwmmacc_vv_f16m16_f8e4m3m1 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f16m16 (acc);
}
vfloat16m8_t
w2_ofp8_fp16_m16_lm8_mixed (vfloat16m8_t lo, vfloat16m8_t hi,
			    vfloat8e4m3m8_t a, vfloat8e5m2m8_t b, size_t vl)
{
  vfloat16m16_t acc = __riscv_ime_vpair_f16m16 (lo, hi);
  acc = __riscv_vfwmmacc_vv_f16m16_f8e4m3m8_f8e5m2m8_lm8 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f16m16 (acc);
}

/* W=2 OFP8 -> BF16.  */
vbfloat16m8_t
w2_ofp8_bf16_m16_lm2 (vbfloat16m8_t lo, vbfloat16m8_t hi,
		      vfloat8e5m2m2_t a, vfloat8e5m2m2_t b, size_t vl)
{
  vbfloat16m16_t acc = __riscv_ime_vpair_bf16m16 (lo, hi);
  acc = __riscv_vfwmmacc_vv_bf16m16_f8e5m2m2_lm2 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_bf16m16 (acc);
}

/* W=4 non-OFP8: FP16 -> FP64, BF16 -> FP64.  */
vfloat64m8_t
w4_f16_m16 (vfloat64m8_t lo, vfloat64m8_t hi,
	    vfloat16m1_t a, vfloat16m1_t b, size_t vl)
{
  vfloat64m16_t acc = __riscv_ime_vpair_f64m16 (lo, hi);
  acc = __riscv_vfqwmmacc_vv_f64m16_f16m1 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f64m16 (acc);
}
vfloat64m8_t
w4_bf16_m16_lm4 (vfloat64m8_t lo, vfloat64m8_t hi,
		 vbfloat16m4_t a, vbfloat16m4_t b, size_t vl)
{
  vfloat64m16_t acc = __riscv_ime_vpair_f64m16 (lo, hi);
  acc = __riscv_vfqwmmacc_vv_f64m16_bf16m4_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f64m16 (acc);
}

/* W=4 OFP8 -> FP32.  */
vfloat32m8_t
w4_ofp8_m16_lm4 (vfloat32m8_t lo, vfloat32m8_t hi,
		 vfloat8e4m3m4_t a, vfloat8e4m3m4_t b, size_t vl)
{
  vfloat32m16_t acc = __riscv_ime_vpair_f32m16 (lo, hi);
  acc = __riscv_vfqwmmacc_vv_f32m16_f8e4m3m4_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f32m16 (acc);
}

/* W=8 OFP8 -> FP64.  */
vfloat64m8_t
w8_ofp8_m16 (vfloat64m8_t lo, vfloat64m8_t hi,
	     vfloat8e4m3m1_t a, vfloat8e4m3m1_t b, size_t vl)
{
  vfloat64m16_t acc = __riscv_ime_vpair_f64m16 (lo, hi);
  acc = __riscv_vf8wmmacc_vv_f64m16_f8e4m3m1 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f64m16 (acc);
}
vfloat64m8_t
w8_ofp8_m16_lm8_mixed (vfloat64m8_t lo, vfloat64m8_t hi,
		       vfloat8e5m2m8_t a, vfloat8e4m3m8_t b, size_t vl)
{
  vfloat64m16_t acc = __riscv_ime_vpair_f64m16 (lo, hi);
  acc = __riscv_vf8wmmacc_vv_f64m16_f8e5m2m8_f8e4m3m8_lm8 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f64m16 (acc);
}

/* 6 vfwmmacc (3 non-OFP8 + 3 OFP8), 3 vfqwmmacc (2 non-OFP8 + 1 OFP8),
   2 vf8wmmacc.  */
/* { dg-final { scan-assembler-times {vfwmmacc\.vv} 6 } } */
/* { dg-final { scan-assembler-times {vfqwmmacc\.vv} 3 } } */
/* { dg-final { scan-assembler-times {vf8wmmacc\.vv} 2 } } */
