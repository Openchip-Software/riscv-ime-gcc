/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* W=4 vqwmmacc.vv: accumulator at SEW=32 (m1 at zvl512b yields
   lambda=4, supported), inputs at QI LMUL=1.  All four sign
   combinations.  */

vint32m1_t
qw_ss (vint32m1_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vqwmmacc_vv_i32m1 (vd, a, b, vl);
}

vuint32m1_t
qw_uu (vuint32m1_t vd, vuint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vqwmmacc_vv_u32m1 (vd, a, b, vl);
}

vint32m1_t
qw_su (vint32m1_t vd, vint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vqwmmacc_vv_i32m1_su (vd, a, b, vl);
}

vint32m1_t
qw_us (vint32m1_t vd, vuint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vqwmmacc_vv_i32m1_us (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvqwmmacc\.vv\t} 4 } } */
/* { dg-final { scan-assembler-times {\tvsetvl\t} 4 } } */
