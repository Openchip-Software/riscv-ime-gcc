/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvfbfmin_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The overloaded forms of the microscaled multiply-accumulates keep the
   block-size suffix, which their arguments do not determine; the OFP8
   ones resolve on the input types.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vfloat16m4_t
ofp8 (vfloat16m4_t vd, vfloat8e5m2m1_t a, vfloat8e4m3m1_t b, size_t vl)
{
  return __riscv_vfwmmacc_vv (vd, a, b, vl);
}

vfloat32m2_t
ofp8_mx (vfloat32m2_t vd, vfloat8e5m2m2_t a, vfloat8e5m2m2_t b, vuint16m1_t s,
	 size_t vl)
{
  return __riscv_vfqmmacc_scaled_vv_bs16 (vd, a, b, s, vl);
}

vfloat32m2_t
ofp4_mx (vfloat32m2_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b, vuint16m1_t s,
	 size_t vl)
{
  return __riscv_vf8wmmacc_scaled_vv_bs32 (vd, a, b, s, vl);
}

vfloat16m4_t
i4_mx (vfloat16m4_t vd, vint4m1_t a, vint4m1_t b, vuint16m1_t s, size_t vl)
{
  return __riscv_vfqimmacc_vv_bs32 (vd, a, b, s, vl);
}

vfloat16m4_t
i8_mx_tu (vfloat16m4_t vd, vint8m2_t a, vint8m2_t b, vuint16m1_t s, size_t vl)
{
  return __riscv_vfwimmacc_vv_bs16_tu (vd, a, b, s, vl);
}

/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvfqmmacc\.vv\t[^\n]*v0\.scale} 1 } } */
/* { dg-final { scan-assembler-times {\tvf8wmmacc\.vv\t[^\n]*v0\.scale} 1 } } */
/* { dg-final { scan-assembler-times {\tvfqimmacc\.vv\t[^\n]*v0\.scale} 1 } } */
/* { dg-final { scan-assembler-times {\tvfwimmacc\.vv\t[^\n]*v0\.scale} 1 } } */
/* ofp8: altfmt_A=1 (E5M2), altfmt_B=0, e16, m1.  */
/* { dg-final { scan-assembler {\tli\tt0,2594073385365405896\n} } } */
