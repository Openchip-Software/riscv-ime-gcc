/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* FP64 accumulator variants.
   VLEN=256, SEW=64, MUL_C=1 -> lambda=sqrt(256/64)=2 -- valid.  */

/* vfwmmacc_f32: FP32 -> FP64.  */
vfloat64m1_t
fwmmacc_f32_f64m1 (vfloat64m1_t vd, vfloat32m1_t a, vfloat32m1_t b,
		    size_t vl)
{
  return __riscv_vfwmmacc_vv_f64m1_f32m1 (vd, a, b, vl);
}

/* vfqwmmacc_f16: FP16 -> FP64.  */
vfloat64m1_t
fqwmmacc_f16_f64m1 (vfloat64m1_t vd, vfloat16m1_t a, vfloat16m1_t b,
		     size_t vl)
{
  return __riscv_vfqwmmacc_vv_f64m1_f16m1 (vd, a, b, vl);
}

/* vf8wmmacc_f8e4m3: OFP8_E4M3 -> FP64.  */
vfloat64m1_t
f8wmmacc_f8e4m3_f64m1 (vfloat64m1_t vd, vfloat8e4m3m1_t a,
			vfloat8e4m3m1_t b, size_t vl)
{
  return __riscv_vf8wmmacc_vv_f64m1_f8e4m3m1 (vd, a, b, vl);
}

/* vf8wmmacc_f8e5m2: OFP8_E5M2 -> FP64.  */
vfloat64m1_t
f8wmmacc_f8e5m2_f64m1 (vfloat64m1_t vd, vfloat8e5m2m1_t a,
			vfloat8e5m2m1_t b, size_t vl)
{
  return __riscv_vf8wmmacc_vv_f64m1_f8e5m2m1 (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvfqwmmacc\.vv\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvf8wmmacc\.vv\t} 2 } } */
