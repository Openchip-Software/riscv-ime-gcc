/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvvfp16fp32mm_zvvbf16fp32mm_zvvfp16fp64mm_zvfh_zvfbfmin -mabi=lp64d -O2" } */

/* Mixed FP16 and BF16 inputs require the extensions of both input
   formats.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vfloat32m1_t
w (vfloat32m1_t vd, vfloat16m1_t a, vbfloat16m1_t b, size_t vl)
{
  return __riscv_vfwmmacc_vv_f32m1_f16m1_bf16m1 (vd, a, b, vl);
}

vfloat64m1_t
q (vfloat64m1_t vd, vfloat16m1_t a, vbfloat16m1_t b, size_t vl)
{
  return __riscv_vfqmmacc_vv_f64m1_f16m1_bf16m1 (vd, a, b, vl); /* { dg-error "requires the 'zvvfp16fp64mm and zvvbf16fp64mm' ISA extension" } */
}

vfloat16m1_t
m (vfloat16m1_t vd, vfloat16m1_t a, vbfloat16m1_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_f16m1_f16m1_bf16m1 (vd, a, b, vl); /* { dg-error "requires the 'zvvfp16mm and zvvbf16mm' ISA extension" } */
}
