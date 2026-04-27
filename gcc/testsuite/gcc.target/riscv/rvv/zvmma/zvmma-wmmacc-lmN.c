/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* _lm{N} variants of the W={2,4,8} widening mmacc family.  vs1/vs2 use
   SEW/W (narrowed) and LMUL=N (independent of MUL_C).  Picked
   (VLEN=256, SEW, MUL_C) triples with valid lambda exponents:
     SEW=32, MUL_C=2 -> VLEN/(SEW*MUL_C)=4 -> lambda=2 (valid)
     SEW=64, MUL_C=4 -> 1 -> lambda=1 (valid)
     SEW=32, MUL_C=4 -> 2 -> rejected (odd log2)? VLEN/SEW/MUL_C = 256/32/4 = 2; not square.
   So we stick to MUL_C=2 for SI accumulators and MUL_C=4 for DI.  */

/* W=2 widening (vwmmacc.vv): i16 acc, i8 inputs.  */
vint32m2_t
wmmacc_s_i32m2_lm2 (vint32m2_t vd, vint16m2_t a, vint16m2_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i32m2_lm2 (vd, a, b, vl);
}

vuint32m2_t
wmmacc_u_u32m2_lm2 (vuint32m2_t vd, vuint16m2_t a, vuint16m2_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_u32m2_lm2 (vd, a, b, vl);
}

vint32m2_t
wmmacc_su_i32m2_lm4 (vint32m2_t vd, vint16m4_t a, vuint16m4_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i32m2_su_lm4 (vd, a, b, vl);
}

vint32m2_t
wmmacc_us_i32m2_lm8 (vint32m2_t vd, vuint16m8_t a, vint16m8_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i32m2_us_lm8 (vd, a, b, vl);
}

/* W=4 widening (vqwmmacc.vv): i32 acc, i8 inputs.  */
vint32m2_t
qwmmacc_s_i32m2_lm2 (vint32m2_t vd, vint8m2_t a, vint8m2_t b, size_t vl)
{
  return __riscv_vqwmmacc_vv_i32m2_lm2 (vd, a, b, vl);
}

vuint32m2_t
qwmmacc_u_u32m2_lm4 (vuint32m2_t vd, vuint8m4_t a, vuint8m4_t b, size_t vl)
{
  return __riscv_vqwmmacc_vv_u32m2_lm4 (vd, a, b, vl);
}

vint32m2_t
qwmmacc_us_i32m2_lm8 (vint32m2_t vd, vuint8m8_t a, vint8m8_t b, size_t vl)
{
  return __riscv_vqwmmacc_vv_i32m2_us_lm8 (vd, a, b, vl);
}

/* W=8 widening (v8wmmacc.vv): i64 acc, i8 inputs.  */
vint64m4_t
v8wmmacc_s_i64m4_lm2 (vint64m4_t vd, vint8m2_t a, vint8m2_t b, size_t vl)
{
  return __riscv_v8wmmacc_vv_i64m4_lm2 (vd, a, b, vl);
}

vint64m4_t
v8wmmacc_su_i64m4_lm4 (vint64m4_t vd, vint8m4_t a, vuint8m4_t b, size_t vl)
{
  return __riscv_v8wmmacc_vv_i64m4_su_lm4 (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvwmmacc\.vv\t}  4 } } */
/* { dg-final { scan-assembler-times {\tvqwmmacc\.vv\t} 3 } } */
/* { dg-final { scan-assembler-times {\tv8wmmacc\.vv\t} 2 } } */
