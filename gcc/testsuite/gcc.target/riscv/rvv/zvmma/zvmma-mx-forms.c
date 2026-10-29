/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvfbfmin_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The microscaled multiply-accumulates of the IME encoding maps: the
   _scaled forms of vfwmmacc, vfqmmacc and vf8wmmacc with OFP4 and OFP8
   inputs, and vfwimmacc, vfqimmacc and vf8wimmacc with Int4 and Int8
   inputs, at both block sizes.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* The spec's examples.  */
vfloat32m2_t
ofp4_fp32 (vfloat32m2_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b,
	   vuint16m1_t s, size_t vl)
{
  return __riscv_vf8wmmacc_scaled_vv_f32m2_f4e2m1m1_bs32 (vd, a, b, s, vl);
}

vbfloat16m1_t
i8_bf16 (vbfloat16m1_t vd, vint8m1_t a, vint8m1_t b, vuint16m1_t s,
	 size_t vl)
{
  return __riscv_vfwimmacc_vv_bf16m1_i8m1_bs32 (vd, a, b, s, vl);
}

/* OFP4 -> E4M3 at SEW=8: lambda=2 for EMUL_C=8.  */
vfloat8e4m3m8_t
ofp4_ofp8 (vfloat8e4m3m8_t vd, vfloat4e2m1m1_t a, vfloat4e2m1m1_t b,
	   vuint16m1_t s, size_t vl)
{
  return __riscv_vfwmmacc_scaled_vv_f8e4m3m8_f4e2m1m1_bs32 (vd, a, b, s, vl);
}

/* Mixed OFP8 inputs, block size 16.  */
vfloat16m4_t
ofp8_fp16_mixed (vfloat16m4_t vd, vfloat8e4m3m1_t a, vfloat8e5m2m1_t b,
		 vuint16m1_t s, size_t vl)
{
  return __riscv_vfwmmacc_scaled_vv_f16m4_f8e4m3m1_f8e5m2m1_bs16 (vd, a, b,
								   s, vl);
}

vbfloat16m4_t
ofp4_bf16_lm2 (vbfloat16m4_t vd, vfloat4e2m1m2_t a, vfloat4e2m1m2_t b,
	       vuint16m1_t s, size_t vl)
{
  return __riscv_vfqmmacc_scaled_vv_bf16m4_f4e2m1m2_lm2_bs16 (vd, a, b, s,
							       vl);
}

vfloat64m1_t
ofp8_fp64 (vfloat64m1_t vd, vfloat8e5m2m1_t a, vfloat8e5m2m1_t b,
	   vuint16m1_t s, size_t vl)
{
  return __riscv_vf8wmmacc_scaled_vv_f64m1_f8e5m2m1_bs32 (vd, a, b, s, vl);
}

vfloat16m4_t
i4_fp16 (vfloat16m4_t vd, vint4m1_t a, vint4m1_t b, vuint16m1_t s, size_t vl)
{
  return __riscv_vfqimmacc_vv_f16m4_i4m1_bs16 (vd, a, b, s, vl);
}

vfloat32m2_t
i4_fp32 (vfloat32m2_t vd, vint4m1_t a, vint4m1_t b, vuint16m1_t s, size_t vl)
{
  return __riscv_vf8wimmacc_vv_f32m2_i4m1_bs32 (vd, a, b, s, vl);
}

/* An m16 accumulator: SEW=16, EMUL_C=16 at VLEN=256: lambda=1.  */
vfloat16m8_t
i8_fp16_m16 (vfloat16m8_t lo, vfloat16m8_t hi, vint8m4_t a, vint8m4_t b,
	     vuint16m1_t s, size_t vl)
{
  vfloat16m16_t acc = __riscv_ime_vpair_f16m16 (lo, hi);
  acc = __riscv_vfwimmacc_vv_f16m16_i8m4_lm4_bs32 (acc, a, b, s, vl);
  return __riscv_ime_vunpairhi_f16m16 (acc);
}

/* { dg-final { scan-assembler-times {\tvf8wmmacc\.vv\tv[0-9]+,v[0-9]+,v[0-9]+,v0\.scale\n} 2 } } */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\tv[0-9]+,v[0-9]+,v[0-9]+,v0\.scale\n} 2 } } */
/* { dg-final { scan-assembler-times {\tvfqmmacc\.vv\tv[0-9]+,v[0-9]+,v[0-9]+,v0\.scale\n} 1 } } */
/* { dg-final { scan-assembler-times {\tvfwimmacc\.vv\tv[0-9]+,v[0-9]+,v[0-9]+,v0\.scale\n} 2 } } */
/* { dg-final { scan-assembler-times {\tvfqimmacc\.vv\tv[0-9]+,v[0-9]+,v[0-9]+,v0\.scale\n} 1 } } */
/* { dg-final { scan-assembler-times {\tvf8wimmacc\.vv\tv[0-9]+,v[0-9]+,v[0-9]+,v0\.scale\n} 1 } } */
/* vtype with bs set for block size 16, altfmt_A/altfmt_B for the E5M2
   inputs and altfmt for the BF16 accumulators: i8_bf16,
   ofp8_fp16_mixed, ofp4_bf16_lm2 and ofp8_fp64.  */
/* { dg-final { scan-assembler {\tli\tt0,3458764513820541384\n} } } */
/* { dg-final { scan-assembler {\tli\tt0,3026418949592973512\n} } } */
/* { dg-final { scan-assembler {\tli\tt0,2882303761517117897\n} } } */
/* { dg-final { scan-assembler {\tli\tt0,2738188573441261784\n} } } */
