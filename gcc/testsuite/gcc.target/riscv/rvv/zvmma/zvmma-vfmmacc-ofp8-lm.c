/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin -mabi=lp64d -O3" } */

/* Regression test for the W=1 OFP8 mmacc `_lm{N}' source-LMUL
   siblings (B.2).  Prior to this commit only the LMUL=1 form was
   enumerated for W=1 OFP8 mmacc; spec § 2657 makes `_lm{N}' apply
   uniformly to the W=1 family, including OFP8.  Both the m1..m8 acc
   and the m16 acc paths are exercised.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

/* W=1 OFP8 m1..m8 acc, same-format e4m3, LMUL=2/4/8.  */
vfloat8e4m3m4_t
ofp8_e4m3_lm2 (vfloat8e4m3m4_t vd, vfloat8e4m3m2_t a, vfloat8e4m3m2_t b,
	       size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m4_f8e4m3m2_lm2 (vd, a, b, vl); }
vfloat8e4m3m4_t
ofp8_e4m3_lm4 (vfloat8e4m3m4_t vd, vfloat8e4m3m4_t a, vfloat8e4m3m4_t b,
	       size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m4_f8e4m3m4_lm4 (vd, a, b, vl); }
vfloat8e4m3m4_t
ofp8_e4m3_lm8 (vfloat8e4m3m4_t vd, vfloat8e4m3m8_t a, vfloat8e4m3m8_t b,
	       size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m4_f8e4m3m8_lm8 (vd, a, b, vl); }

/* W=1 OFP8 m1..m8 acc, mixed-format (vs1=e4m3, vs2=e5m2), LMUL=2/4/8.  */
vfloat8e5m2m4_t
ofp8_mixed_lm2 (vfloat8e5m2m4_t vd, vfloat8e4m3m2_t a, vfloat8e5m2m2_t b,
		size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m4_f8e4m3m2_f8e5m2m2_lm2 (vd, a, b, vl); }
vfloat8e5m2m4_t
ofp8_mixed_lm4 (vfloat8e5m2m4_t vd, vfloat8e4m3m4_t a, vfloat8e5m2m4_t b,
		size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m4_f8e4m3m4_f8e5m2m4_lm4 (vd, a, b, vl); }
vfloat8e5m2m4_t
ofp8_mixed_lm8 (vfloat8e5m2m4_t vd, vfloat8e4m3m8_t a, vfloat8e5m2m8_t b,
		size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m4_f8e4m3m8_f8e5m2m8_lm8 (vd, a, b, vl); }

/* W=1 OFP8 m16 acc, same-format e4m3, LMUL=2/4/8.  */
vfloat8e4m3m8_t
ofp8_m16_e4m3_lm2 (vfloat8e4m3m8_t vd_lo, vfloat8e4m3m8_t vd_hi,
		   vfloat8e4m3m2_t a, vfloat8e4m3m2_t b, size_t vl)
{
  vfloat8e4m3m16_t acc = __riscv_ime_vpair_f8e4m3m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f8e4m3m16_f8e4m3m2_lm2 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f8e4m3m16 (acc);
}
vfloat8e4m3m8_t
ofp8_m16_e4m3_lm4 (vfloat8e4m3m8_t vd_lo, vfloat8e4m3m8_t vd_hi,
		   vfloat8e4m3m4_t a, vfloat8e4m3m4_t b, size_t vl)
{
  vfloat8e4m3m16_t acc = __riscv_ime_vpair_f8e4m3m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f8e4m3m16_f8e4m3m4_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f8e4m3m16 (acc);
}
vfloat8e4m3m8_t
ofp8_m16_e4m3_lm8 (vfloat8e4m3m8_t vd_lo, vfloat8e4m3m8_t vd_hi,
		   vfloat8e4m3m8_t a, vfloat8e4m3m8_t b, size_t vl)
{
  vfloat8e4m3m16_t acc = __riscv_ime_vpair_f8e4m3m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f8e4m3m16_f8e4m3m8_lm8 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f8e4m3m16 (acc);
}

/* W=1 OFP8 m16 acc, mixed-format (vs1=e4m3, vs2=e5m2), LMUL=4.  */
vfloat8e5m2m8_t
ofp8_m16_mixed_lm4 (vfloat8e5m2m8_t vd_lo, vfloat8e5m2m8_t vd_hi,
		    vfloat8e4m3m4_t a, vfloat8e5m2m4_t b, size_t vl)
{
  vfloat8e5m2m16_t acc = __riscv_ime_vpair_f8e5m2m16 (vd_lo, vd_hi);
  acc = __riscv_vfmmacc_vv_f8e5m2m16_f8e4m3m4_f8e5m2m4_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f8e5m2m16 (acc);
}

/* 6 m1..m8 + 4 m16 = 10 vfmmacc.vv expected.  */
/* { dg-final { scan-assembler-times {vfmmacc\.vv} 10 } } */
