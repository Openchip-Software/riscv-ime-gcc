/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin -mabi=lp64d -O3" } */

/* Regression test for the W=1 FP / OFP8 `m16' accumulator path and
   the FP / OFP8 `__riscv_ime_vpair' / `__riscv_ime_vunpair{lo,hi}'
   primitives.  Prior to this commit, only the integer m16 surface
   existed; the FP / OFP8 m16 acc intrinsics were not declared.

   m16 types are forbidden at function boundaries, so each test pairs
   two m8 halves into an m16 acc, runs vfmmacc, then unpairs and
   reduces to an m8 return value.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

/* W=1 vfmmacc, FP32 m16 acc.  Default convention (m1 inputs).  */
vfloat32m8_t
w1_f32_m16 (vfloat32m8_t vd_lo, vfloat32m8_t vd_hi,
	    vfloat32m1_t a, vfloat32m1_t b, size_t vl)
{
  vfloat32m16_t acc = __riscv_ime_vpair_f32m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f32m16 (acc, a, b, vl);
  return __riscv_vfadd_vv_f32m8 (__riscv_ime_vunpairlo_f32m16 (acc),
				 __riscv_ime_vunpairhi_f32m16 (acc), vl);
}

/* W=1 vfmmacc, FP16 m16 acc, default convention.  */
vfloat16m8_t
w1_f16_m16 (vfloat16m8_t vd_lo, vfloat16m8_t vd_hi,
	    vfloat16m1_t a, vfloat16m1_t b, size_t vl)
{
  vfloat16m16_t acc = __riscv_ime_vpair_f16m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f16m16 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f16m16 (acc);
}

/* W=1 vfmmacc, BF16 m16 acc.  */
vbfloat16m8_t
w1_bf16_m16 (vbfloat16m8_t vd_lo, vbfloat16m8_t vd_hi,
	     vbfloat16m1_t a, vbfloat16m1_t b, size_t vl)
{
  vbfloat16m16_t acc = __riscv_ime_vpair_bf16m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_bf16m16 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_bf16m16 (acc);
}

/* W=1 vfmmacc, FP32 m16 acc with _lm{2,4,8} source-LMUL forms.  */
vfloat32m8_t
w1_f32_m16_lm2 (vfloat32m8_t vd_lo, vfloat32m8_t vd_hi,
		vfloat32m2_t a, vfloat32m2_t b, size_t vl)
{
  vfloat32m16_t acc = __riscv_ime_vpair_f32m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f32m16_lm2 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f32m16 (acc);
}

vfloat32m8_t
w1_f32_m16_lm4 (vfloat32m8_t vd_lo, vfloat32m8_t vd_hi,
		vfloat32m4_t a, vfloat32m4_t b, size_t vl)
{
  vfloat32m16_t acc = __riscv_ime_vpair_f32m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f32m16_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f32m16 (acc);
}

vfloat32m8_t
w1_f32_m16_lm8 (vfloat32m8_t vd_lo, vfloat32m8_t vd_hi,
		vfloat32m8_t a, vfloat32m8_t b, size_t vl)
{
  vfloat32m16_t acc = __riscv_ime_vpair_f32m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f32m16_lm8 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f32m16 (acc);
}

/* W=1 OFP8 vfmmacc, e4m3 m16 acc with same-format inputs.  */
vfloat8e4m3m8_t
w1_ofp8_e4m3_m16 (vfloat8e4m3m8_t vd_lo, vfloat8e4m3m8_t vd_hi,
		  vfloat8e4m3m1_t a, vfloat8e4m3m1_t b, size_t vl)
{
  vfloat8e4m3m16_t acc = __riscv_ime_vpair_f8e4m3m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f8e4m3m16_f8e4m3m1 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f8e4m3m16 (acc);
}

/* W=1 OFP8 vfmmacc, e5m2 m16 acc with mixed-format inputs.  */
vfloat8e5m2m8_t
w1_ofp8_e5m2_m16_mixed (vfloat8e5m2m8_t vd_lo, vfloat8e5m2m8_t vd_hi,
			vfloat8e4m3m1_t a, vfloat8e5m2m1_t b, size_t vl)
{
  vfloat8e5m2m16_t acc = __riscv_ime_vpair_f8e5m2m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f8e5m2m16_f8e4m3m1_f8e5m2m1 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f8e5m2m16 (acc);
}

/* { dg-final { scan-assembler-times {vfmmacc\.vv} 8 } } */
