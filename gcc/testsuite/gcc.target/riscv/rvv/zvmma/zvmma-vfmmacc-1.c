/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* vfmmacc.vv (W=1): FP same-format multiply-accumulate.
   At VLEN=256, SEW=32, MUL_C=1 -> lambda=sqrt(256/(32*1))=sqrt(8) -- not
   a valid lambda.  SEW=16, MUL_C=1 -> lambda=sqrt(256/16)=4 -- valid!
   So use f16m1 for the W=1 test.  */

vfloat16m1_t
fmmacc_f16m1 (vfloat16m1_t vd, vfloat16m1_t a, vfloat16m1_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_f16m1 (vd, a, b, vl);
}

/* vfwmmacc.vv (W=2): FP16 -> FP32.
   VLEN=256, SEW(acc)=32, MUL_C=1 -> lambda=sqrt(256/32)=sqrt(8) -- invalid.
   Use m2: SEW=32, MUL_C=2 -> lambda=sqrt(256/64)=2 -- valid!  */

vfloat32m2_t
fwmmacc_f16_f32m2 (vfloat32m2_t vd, vfloat16m1_t a, vfloat16m1_t b,
		    size_t vl)
{
  return __riscv_vfwmmacc_vv_f32m2_f16m1 (vd, a, b, vl);
}

/* vfqwmmacc.vv (W=4): OFP8_E4M3 -> FP32.
   VLEN=256, SEW=32, MUL_C=2 -> lambda=2.  */

vfloat32m2_t
fqwmmacc_f8e4m3_f32m2 (vfloat32m2_t vd, vfloat8e4m3m1_t a,
			vfloat8e4m3m1_t b, size_t vl)
{
  return __riscv_vfqwmmacc_vv_f32m2_f8e4m3m1 (vd, a, b, vl);
}

/* vfqwmmacc.vv (W=4): OFP8_E5M2 -> FP32.  */

vfloat32m2_t
fqwmmacc_f8e5m2_f32m2 (vfloat32m2_t vd, vfloat8e5m2m1_t a,
			vfloat8e5m2m1_t b, size_t vl)
{
  return __riscv_vfqwmmacc_vv_f32m2_f8e5m2m1 (vd, a, b, vl);
}

/* vf8wmmacc.vv (W=8): OFP4_E2M1 -> FP32.  */

vfloat32m2_t
f8wmmacc_f4e2m1_f32m2 (vfloat32m2_t vd, vfloat4e2m1m1_t a,
			vfloat4e2m1m1_t b, size_t vl)
{
  return __riscv_vf8wmmacc_vv_f32m2_f4e2m1m1 (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvfmmacc\.vv\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvfqwmmacc\.vv\t} 2 } } */
/* { dg-final { scan-assembler-times {\tvf8wmmacc\.vv\t} 1 } } */
