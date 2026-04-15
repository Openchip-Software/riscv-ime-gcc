/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* W=2 vwmmacc.vv: accumulator at SEW (i16m2), inputs at SEW/2 LMUL=1.
   All four sign combinations must emit vwmmacc.vv with a distinct
   vtype (altfmt_A / altfmt_B encoded via ime_altfmt).  */

vint16m2_t
vwmmacc_ss (vint16m2_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2 (vd, a, b, vl);
}

vuint16m2_t
vwmmacc_uu (vuint16m2_t vd, vuint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_u16m2 (vd, a, b, vl);
}

vint16m2_t
vwmmacc_su (vint16m2_t vd, vint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2_su (vd, a, b, vl);
}

vint16m2_t
vwmmacc_us (vint16m2_t vd, vuint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2_us (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvwmmacc\.vv\t} 4 } } */
/* { dg-final { scan-assembler-times {\tvsetvl\t} 4 } } */
/* { dg-final { scan-assembler-not {csrr\tt[0-9]*,vtype} } } */
