/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* W=4 vqmmacc.vv: accumulator at SEW=32 (m1 at zvl512b yields
   lambda=4, supported), inputs at QI LMUL=1.  All four sign
   combinations.  */

vint32m1_t
q_ss (vint32m1_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv_i32m1 (vd, a, b, vl);
}

vuint32m1_t
q_uu (vuint32m1_t vd, vuint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv_u32m1 (vd, a, b, vl);
}

vint32m1_t
q_su (vint32m1_t vd, vint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv_i32m1_su (vd, a, b, vl);
}

vint32m1_t
q_us (vint32m1_t vd, vuint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv_i32m1_us (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvqmmacc\.vv\t} 4 } } */
/* { dg-final { scan-assembler-times {\tvsetvl\t} 4 } } */
