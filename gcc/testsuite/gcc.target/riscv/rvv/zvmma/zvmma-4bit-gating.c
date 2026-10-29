/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvvi8i16mm_zvvofp4fp16mm_zvfh -mabi=lp64d -O2" } */

/* Each 4-bit multiply-accumulate requires its own extension.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint16m1_t
i8i16 (vint16m1_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m1 (vd, a, b, vl);
}

vint8m1_t
i4i8 (vint8m1_t vd, vint4m1_t a, vint4m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i8m1_i4m1 (vd, a, b, vl); /* { dg-error "requires the 'zvvi4i8mm' ISA extension" } */
}

vfloat16m1_t
ofp4fp16 (vfloat16m1_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b, size_t vl)
{
  return __riscv_vfqmmacc_vv_f16m1_f4e2m1m1 (vd, a, b, vl);
}

vfloat32m1_t
ofp4fp32 (vfloat32m1_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b, size_t vl)
{
  return __riscv_vf8wmmacc_vv_f32m1_f4e2m1m1 (vd, a, b, vl); /* { dg-error "requires the 'zvvofp4fp32mm' ISA extension" } */
}
