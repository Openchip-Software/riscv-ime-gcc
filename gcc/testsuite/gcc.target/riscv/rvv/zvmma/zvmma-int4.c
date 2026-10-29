/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The integer multiply-accumulates with 4-bit inputs: Int4 -> Int8
   (vwmmacc), Int4 -> Int16 (vqmmacc) and Int4 -> Int32 (v8wmmacc),
   named with their input type.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* SEW=8, EMUL_C=8 at VLEN=256: lambda=2.  */
vint8m8_t
w (vint8m8_t vd, vint4m1_t a, vint4m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i8m8_i4m1 (vd, a, b, vl);
}

/* Signed A, unsigned B: altfmt_B=1.  */
vint8m8_t
w_su (vint8m8_t vd, vint4m1_t a, vuint4m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i8m8_i4m1_su (vd, a, b, vl);
}

/* Unsigned inputs at LMUL=2; SEW=8, EMUL_C=2: lambda=4.  */
vuint8m2_t
w_uu_lm2 (vuint8m2_t vd, vuint4m2_t a, vuint4m2_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_u8m2_u4m2_lm2 (vd, a, b, vl);
}

vint16m4_t
q (vint16m4_t vd, vint4m1_t a, vint4m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv_i16m4_i4m1 (vd, a, b, vl);
}

vint16m4_t
q_us_overloaded (vint16m4_t vd, vuint4m1_t a, vint4m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv (vd, a, b, vl);
}

/* SEW=32, EMUL_C=2: lambda=2.  */
vint32m2_t
o (vint32m2_t vd, vint4m4_t a, vint4m4_t b, size_t vl)
{
  return __riscv_v8wmmacc_vv_i32m2_i4m4_lm4 (vd, a, b, vl);
}

/* An m16 accumulator: SEW=16, EMUL_C=16 at VLEN=256: lambda=1.  */
vint16m8_t
q_m16 (vint16m8_t lo, vint16m8_t hi, vint4m1_t a, vint4m1_t b, size_t vl)
{
  vint16m16_t acc = __riscv_ime_vpair_i16m16 (lo, hi);
  acc = __riscv_vqmmacc_vv_i16m16_i4m1 (acc, a, b, vl);
  return __riscv_ime_vunpairlo_i16m16 (acc);
}

/* { dg-final { scan-assembler-times {\tvwmmacc\.vv\t} 3 } } */
/* { dg-final { scan-assembler-times {\tvqmmacc\.vv\t} 3 } } */
/* { dg-final { scan-assembler-times {\tv8wmmacc\.vv\t} 1 } } */
/* vtype (lambda encoding 2, e8, m1, ta, ma) of w, with altfmt_B set
   for w_su, and that of q_us_overloaded (e16, altfmt_A set).  */
/* { dg-final { scan-assembler {\tli\tt0,2305843009213694144\n} } } */
/* { dg-final { scan-assembler {\tli\tt0,2449958197289550016\n} } } */
/* { dg-final { scan-assembler {\tli\tt0,2594073385365405896\n} } } */
