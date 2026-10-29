/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvfbfmin_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The FP multiply-accumulates with mixed FP16 and BF16 inputs, which
   set altfmt_A and altfmt_B independently.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vfloat16m4_t
m_f16 (vfloat16m4_t vd, vfloat16m1_t a, vbfloat16m1_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_f16m4_f16m1_bf16m1 (vd, a, b, vl);
}

vfloat16m4_t
m_f16_ba (vfloat16m4_t vd, vbfloat16m1_t a, vfloat16m1_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_f16m4_bf16m1_f16m1 (vd, a, b, vl);
}

vbfloat16m4_t
m_bf16 (vbfloat16m4_t vd, vfloat16m1_t a, vbfloat16m1_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_bf16m4_f16m1_bf16m1 (vd, a, b, vl);
}

vbfloat16m4_t
m_bf16_ba (vbfloat16m4_t vd, vbfloat16m1_t a, vfloat16m1_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_bf16m4_bf16m1_f16m1 (vd, a, b, vl);
}

/* The spec's example and its overloaded form.  */
vfloat32m2_t
w (vfloat32m2_t vd, vfloat16m1_t a, vbfloat16m1_t b, size_t vl)
{
  return __riscv_vfwmmacc_vv_f32m2_f16m1_bf16m1 (vd, a, b, vl);
}

vfloat32m2_t
w_overloaded (vfloat32m2_t vd, vbfloat16m2_t a, vfloat16m2_t b, size_t vl)
{
  return __riscv_vfwmmacc_vv (vd, a, b, vl);
}

vfloat64m1_t
q (vfloat64m1_t vd, vfloat16m1_t a, vbfloat16m1_t b, size_t vl)
{
  return __riscv_vfqmmacc_vv_f64m1_f16m1_bf16m1 (vd, a, b, vl);
}

/* An m16 accumulator: SEW=16, EMUL_C=16 at VLEN=256: lambda=1.  */
vfloat16m8_t
m_m16 (vfloat16m8_t lo, vfloat16m8_t hi, vbfloat16m4_t a, vfloat16m4_t b,
       size_t vl)
{
  vfloat16m16_t acc = __riscv_ime_vpair_f16m16 (lo, hi);
  acc = __riscv_vfmmacc_vv_f16m16_bf16m4_f16m4_lm4 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_f16m16 (acc);
}

/* { dg-final { scan-assembler-times {\tvfmmacc\.vv\t} 5 } } */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 2 } } */
/* { dg-final { scan-assembler-times {\tvfqmmacc\.vv\t} 1 } } */
