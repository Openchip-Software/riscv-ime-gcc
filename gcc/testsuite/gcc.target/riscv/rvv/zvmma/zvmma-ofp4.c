/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvfbfmin_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The FP multiply-accumulates with OFP4 (E2M1) inputs: -> E4M3 and
   E5M2 (vfwmmacc), -> FP16 and BF16 (vfqmmacc) and -> FP32
   (vf8wmmacc).  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vfloat8e4m3m8_t
w_e4m3 (vfloat8e4m3m8_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b, size_t vl)
{
  return __riscv_vfwmmacc_vv_f8e4m3m8_f4e2m1m1 (vd, a, b, vl);
}

/* The E5M2 accumulator sets altfmt.  */
vfloat8e5m2m8_t
w_e5m2 (vfloat8e5m2m8_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b, size_t vl)
{
  return __riscv_vfwmmacc_vv_f8e5m2m8_f4e2m1m1 (vd, a, b, vl);
}

vfloat16m4_t
q_f16 (vfloat16m4_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b, size_t vl)
{
  return __riscv_vfqmmacc_vv_f16m4_f4e2m1m1 (vd, a, b, vl);
}

vbfloat16m4_t
q_bf16_lm2 (vbfloat16m4_t vd, vfloat4e2m1m2_t a, vfloat4e2m1m2_t b,
	    size_t vl)
{
  return __riscv_vfqmmacc_vv_bf16m4_f4e2m1m2_lm2 (vd, a, b, vl);
}

vfloat32m2_t
o (vfloat32m2_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b, size_t vl)
{
  return __riscv_vf8wmmacc_vv (vd, a, b, vl);
}

/* An m16 accumulator: SEW=16, EMUL_C=16 at VLEN=256: lambda=1.  */
vfloat16m8_t
q_m16 (vfloat16m8_t lo, vfloat16m8_t hi, vfloat4e2m1m8_t a,
       vfloat4e2m1m8_t b, size_t vl)
{
  vfloat16m16_t acc = __riscv_ime_vpair_f16m16 (lo, hi);
  acc = __riscv_vfqmmacc_vv_f16m16_f4e2m1m8_lm8 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f16m16 (acc);
}

/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 2 } } */
/* { dg-final { scan-assembler-times {\tvfqmmacc\.vv\t} 3 } } */
/* { dg-final { scan-assembler-times {\tvf8wmmacc\.vv\t} 1 } } */
