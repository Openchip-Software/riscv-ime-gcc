/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Phase A _lm{N} variants of the W=1 mmacc family.  vs1/vs2 use LMUL=N
   at the same SEW as the accumulator (independent of MUL_C).  At
   VLEN=256, SEW=32, MUL_C=2 yields lambda=2 (valid); MUL_C=8 yields
   lambda=1 (valid), so we can exercise the i32m2 accumulator with all
   three source LMULs.  */

/* Signed integer vmmacc, i32m2 accumulator, source LMUL in {2, 4, 8}.  */
vint32m2_t
mmacc_s_i32m2_lm2 (vint32m2_t vd, vint32m2_t a, vint32m2_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_lm2 (vd, a, b, vl);
}

vint32m2_t
mmacc_s_i32m2_lm4 (vint32m2_t vd, vint32m4_t a, vint32m4_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_lm4 (vd, a, b, vl);
}

vint32m2_t
mmacc_s_i32m2_lm8 (vint32m2_t vd, vint32m8_t a, vint32m8_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_lm8 (vd, a, b, vl);
}

/* Unsigned integer vmmacc, u32m2 accumulator.  */
vuint32m2_t
mmacc_u_u32m2_lm2 (vuint32m2_t vd, vuint32m2_t a, vuint32m2_t b, size_t vl)
{
  return __riscv_vmmacc_vv_u32m2_lm2 (vd, a, b, vl);
}

vuint32m2_t
mmacc_u_u32m2_lm4 (vuint32m2_t vd, vuint32m4_t a, vuint32m4_t b, size_t vl)
{
  return __riscv_vmmacc_vv_u32m2_lm4 (vd, a, b, vl);
}

vuint32m2_t
mmacc_u_u32m2_lm8 (vuint32m2_t vd, vuint32m8_t a, vuint32m8_t b, size_t vl)
{
  return __riscv_vmmacc_vv_u32m2_lm8 (vd, a, b, vl);
}

/* FP vfmmacc, f32m2 accumulator (the user's original failing case).  */
vfloat32m2_t
fmmacc_f32m2_lm2 (vfloat32m2_t vd, vfloat32m2_t a, vfloat32m2_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_f32m2_lm2 (vd, a, b, vl);
}

vfloat32m2_t
fmmacc_f32m2_lm4 (vfloat32m2_t vd, vfloat32m4_t a, vfloat32m4_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_f32m2_lm4 (vd, a, b, vl);
}

vfloat32m2_t
fmmacc_f32m2_lm8 (vfloat32m2_t vd, vfloat32m8_t a, vfloat32m8_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_f32m2_lm8 (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvmmacc\.vv\t}  6 } } */
/* { dg-final { scan-assembler-times {\tvfmmacc\.vv\t} 3 } } */
