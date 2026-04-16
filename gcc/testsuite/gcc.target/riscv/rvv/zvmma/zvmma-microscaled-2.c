/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Microscaled section 8 FP64-accumulator variants.  */

/* vf8wmmacc_scaled OFP8_E4M3 -> FP64, BS=32.
   VLEN=256, SEW(acc)=64, MUL_C=2 -> lambda=sqrt(256/128)=sqrt(2)
   -- invalid for MUL_C=2.  Try MUL_C=4:
   VLEN=256, SEW=64, MUL_C=4 -> lambda=sqrt(256/256)=1 -- valid!  */

vfloat64m4_t
f8wmmacc_scaled_f8e4m3_bs32 (vfloat64m4_t vd, vfloat8e4m3m1_t a,
			      vfloat8e4m3m1_t b, vuint16m1_t v0, size_t vl)
{
  return __riscv_vf8wmmacc_scaled_vv_f64m4_f8e4m3m1_bs32 (vd, a, b, v0, vl);
}

/* vf8wimmacc Int8 -> FP64, BS=32.  */

vfloat64m4_t
f8wimmacc_i8_bs32 (vfloat64m4_t vd, vint8m1_t a, vint8m1_t b,
		    vuint16m1_t v0, size_t vl)
{
  return __riscv_vf8wimmacc_vv_f64m4_i8m1_bs32 (vd, a, b, v0, vl);
}

/* { dg-final { scan-assembler-times {\tvf8wmmacc\.vv\t[^\n]*,v0\.t} 1 } } */
/* { dg-final { scan-assembler-times {\tvf8wimmacc\.vv\t[^\n]*,v0\.t} 1 } } */
