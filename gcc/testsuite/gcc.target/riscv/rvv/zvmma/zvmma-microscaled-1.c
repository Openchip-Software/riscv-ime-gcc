/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Microscaled FP-input scaled variants (section 8).
   These emit the same mnemonic as the section 7 FP mmacc but with ,v0.t
   suffix to indicate the block-scale vector is present in v0.  */

/* vfwmmacc_scaled FP16 -> FP32, BS=32.
   VLEN=256, SEW(acc)=32, MUL_C=2 -> lambda=2.  */

vfloat32m2_t
fwmmacc_scaled_f16_bs32 (vfloat32m2_t vd, vfloat16m1_t a, vfloat16m1_t b,
			  vuint16m1_t v0, size_t vl)
{
  return __riscv_vfwmmacc_scaled_vv_f32m2_f16m1_bs32 (vd, a, b, v0, vl);
}

/* vfwmmacc_scaled FP16 -> FP32, BS=16.  */

vfloat32m2_t
fwmmacc_scaled_f16_bs16 (vfloat32m2_t vd, vfloat16m1_t a, vfloat16m1_t b,
			  vuint16m1_t v0, size_t vl)
{
  return __riscv_vfwmmacc_scaled_vv_f32m2_f16m1_bs16 (vd, a, b, v0, vl);
}

/* vfqwmmacc_scaled OFP8_E4M3 -> FP32, BS=32.  */

vfloat32m2_t
fqwmmacc_scaled_f8e4m3_bs32 (vfloat32m2_t vd, vfloat8e4m3m1_t a,
			      vfloat8e4m3m1_t b, vuint16m1_t v0, size_t vl)
{
  return __riscv_vfqwmmacc_scaled_vv_f32m2_f8e4m3m1_bs32 (vd, a, b, v0, vl);
}

/* vfqwmmacc_scaled OFP8_E4M3 -> FP32, BS=16.  */

vfloat32m2_t
fqwmmacc_scaled_f8e4m3_bs16 (vfloat32m2_t vd, vfloat8e4m3m1_t a,
			      vfloat8e4m3m1_t b, vuint16m1_t v0, size_t vl)
{
  return __riscv_vfqwmmacc_scaled_vv_f32m2_f8e4m3m1_bs16 (vd, a, b, v0, vl);
}

/* Microscaled integer-input variants (new instructions).  */

/* vfwimmacc Int8 -> FP16, BS=32.
   VLEN=256, SEW(acc)=16, MUL_C=1 -> lambda=4.  */

vfloat16m1_t
fwimmacc_i8_bs32 (vfloat16m1_t vd, vint8m1_t a, vint8m1_t b,
		   vuint16m1_t v0, size_t vl)
{
  return __riscv_vfwimmacc_vv_f16m1_i8m1_bs32 (vd, a, b, v0, vl);
}

/* vfwimmacc Int8 -> FP16, BS=16.  */

vfloat16m1_t
fwimmacc_i8_bs16 (vfloat16m1_t vd, vint8m1_t a, vint8m1_t b,
		   vuint16m1_t v0, size_t vl)
{
  return __riscv_vfwimmacc_vv_f16m1_i8m1_bs16 (vd, a, b, v0, vl);
}

/* vfqwimmacc Int8 -> FP32, BS=32.
   VLEN=256, SEW(acc)=32, MUL_C=2 -> lambda=2.  */

vfloat32m2_t
fqwimmacc_i8_bs32 (vfloat32m2_t vd, vint8m1_t a, vint8m1_t b,
		    vuint16m1_t v0, size_t vl)
{
  return __riscv_vfqwimmacc_vv_f32m2_i8m1_bs32 (vd, a, b, v0, vl);
}

/* vfqwimmacc Int8 -> FP32, BS=16.  */

vfloat32m2_t
fqwimmacc_i8_bs16 (vfloat32m2_t vd, vint8m1_t a, vint8m1_t b,
		    vuint16m1_t v0, size_t vl)
{
  return __riscv_vfqwimmacc_vv_f32m2_i8m1_bs16 (vd, a, b, v0, vl);
}

/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t[^\n]*,v0\.t} 2 } } */
/* { dg-final { scan-assembler-times {\tvfqwmmacc\.vv\t[^\n]*,v0\.t} 2 } } */
/* { dg-final { scan-assembler-times {\tvfwimmacc\.vv\t[^\n]*,v0\.t} 2 } } */
/* { dg-final { scan-assembler-times {\tvfqwimmacc\.vv\t[^\n]*,v0\.t} 2 } } */
