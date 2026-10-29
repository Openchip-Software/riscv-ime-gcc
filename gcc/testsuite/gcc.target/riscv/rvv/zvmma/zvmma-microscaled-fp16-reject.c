/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma_zvfh -mabi=lp64d -O2" } */

/* Microscaling is defined only for narrow inputs (OFP4, OFP8, Int4 and
   Int8), so there is no microscaled FP16 x FP16 -> FP32
   multiply-accumulate.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vfloat32m2_t
f1 (vfloat32m2_t vd, vfloat16m1_t a, vfloat16m1_t b, vuint16m1_t sc,
    size_t vl)
{
  return __riscv_vfwmmacc_scaled_vv_f32m2_f16m1_bs32 (vd, a, b, sc, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}

vfloat32m2_t
f2 (vfloat32m2_t vd, vfloat16m1_t a, vfloat16m1_t b, vuint16m1_t sc,
    size_t vl)
{
  return __riscv_vfwmmacc_scaled_vv_f32m2_f16m1_bs16 (vd, a, b, sc, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}
