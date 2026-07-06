/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin -mabi=lp64d -O3" } */

/* Regression test for Zvvmm widening m16 accumulator mmacc (#489).
   Exercises vwmmacc (W=2), vqwmmacc (W=4), and v8wmmacc (W=8) at the
   m16 accumulator across the four sign combinations and default /
   _lm{2,4,8} source-LMUL siblings.

   m16 crosses the ABI boundary only via ime_vpair / ime_vunpair, so
   each test pairs two m8 halves into an m16 acc, runs the widening
   mmacc, then unpairs.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

/* W=2 (vwmmacc) m16 acc.  */
vint32m8_t
w2_s_m16 (vint32m8_t lo, vint32m8_t hi, vint16m1_t a, vint16m1_t b, size_t vl)
{
  vint32m16_t acc = __riscv_ime_vpair_i32m16 (lo, hi);
  acc = __riscv_vwmmacc_vv_i32m16 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_i32m16 (acc);
}
vuint32m8_t
w2_u_m16_lm4 (vuint32m8_t lo, vuint32m8_t hi,
	      vuint16m4_t a, vuint16m4_t b, size_t vl)
{
  vuint32m16_t acc = __riscv_ime_vpair_u32m16 (lo, hi);
  acc = __riscv_vwmmacc_vv_u32m16_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_u32m16 (acc);
}
vint32m8_t
w2_su_m16_lm2 (vint32m8_t lo, vint32m8_t hi,
	       vint16m2_t a, vuint16m2_t b, size_t vl)
{
  vint32m16_t acc = __riscv_ime_vpair_i32m16 (lo, hi);
  acc = __riscv_vwmmacc_vv_i32m16_su_lm2 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_i32m16 (acc);
}
vint32m8_t
w2_us_m16_lm8 (vint32m8_t lo, vint32m8_t hi,
	       vuint16m8_t a, vint16m8_t b, size_t vl)
{
  vint32m16_t acc = __riscv_ime_vpair_i32m16 (lo, hi);
  acc = __riscv_vwmmacc_vv_i32m16_us_lm8 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_i32m16 (acc);
}

/* W=4 (vqwmmacc) m16 acc.  */
vint32m8_t
w4_s_m16 (vint32m8_t lo, vint32m8_t hi, vint8m1_t a, vint8m1_t b, size_t vl)
{
  vint32m16_t acc = __riscv_ime_vpair_i32m16 (lo, hi);
  acc = __riscv_vqwmmacc_vv_i32m16 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_i32m16 (acc);
}
vuint32m8_t
w4_u_m16_lm4 (vuint32m8_t lo, vuint32m8_t hi,
	      vuint8m4_t a, vuint8m4_t b, size_t vl)
{
  vuint32m16_t acc = __riscv_ime_vpair_u32m16 (lo, hi);
  acc = __riscv_vqwmmacc_vv_u32m16_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_u32m16 (acc);
}
vint64m8_t
w4_su_m16_lm2 (vint64m8_t lo, vint64m8_t hi,
	       vint16m2_t a, vuint16m2_t b, size_t vl)
{
  vint64m16_t acc = __riscv_ime_vpair_i64m16 (lo, hi);
  acc = __riscv_vqwmmacc_vv_i64m16_su_lm2 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_i64m16 (acc);
}

/* W=8 (v8wmmacc) m16 acc.  */
vint64m8_t
w8_s_m16 (vint64m8_t lo, vint64m8_t hi, vint8m1_t a, vint8m1_t b, size_t vl)
{
  vint64m16_t acc = __riscv_ime_vpair_i64m16 (lo, hi);
  acc = __riscv_v8wmmacc_vv_i64m16 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_i64m16 (acc);
}
vuint64m8_t
w8_u_m16_lm8 (vuint64m8_t lo, vuint64m8_t hi,
	      vuint8m8_t a, vuint8m8_t b, size_t vl)
{
  vuint64m16_t acc = __riscv_ime_vpair_u64m16 (lo, hi);
  acc = __riscv_v8wmmacc_vv_u64m16_lm8 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_u64m16 (acc);
}

/* 4 vwmmacc + 3 vqwmmacc + 2 v8wmmacc.  */
/* { dg-final { scan-assembler-times {vwmmacc\.vv} 4 } } */
/* { dg-final { scan-assembler-times {vqwmmacc\.vv} 3 } } */
/* { dg-final { scan-assembler-times {v8wmmacc\.vv} 2 } } */
