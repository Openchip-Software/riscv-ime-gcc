/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* After `__riscv_vsetlambda', a mixed-sign multiply-accumulate still
   needs its IME vsetvl: besides vl, SEW and LMUL it sets altfmt_B for
   the unsigned B input (0x22000000000000d0: lambda=2, altfmt_B, e32,
   m1, ta, ma).  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint32m2_t
wsu (vint32m2_t vd, vint16m1_t a, vuint16m1_t b, size_t vl)
{
  __riscv_vsetlambda (2);
  return __riscv_vwmmacc_vv_i32m2_su (vd, a, b, vl);
}

/* { dg-final { scan-assembler {li\tt0,2449958197289550032\n\tvsetvl\tt0,a0,t0\n\tvwmmacc\.vv} } } */
