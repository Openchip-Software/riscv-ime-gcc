/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Each sign combination must produce a DIFFERENT full vtype constant
   (distinct altfmt_A/altfmt_B bits).  Test that all four constants
   are distinct by checking that each specific 'li t0,<imm>' sequence
   appears exactly once across the four functions.  */

vint16m2_t
ss (vint16m2_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2 (vd, a, b, vl);
}

vuint16m2_t
uu (vuint16m2_t vd, vuint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_u16m2 (vd, a, b, vl);
}

vint16m2_t
su (vint16m2_t vd, vint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2_su (vd, a, b, vl);
}

vint16m2_t
us (vint16m2_t vd, vuint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2_us (vd, a, b, vl);
}

/* Four distinct vtype constants -> four distinct 'li t0,<big imm>'
   instructions feeding the register-form vsetvl.  The constants encode
   SEW=16, LMUL=m1 (source LMUL, NOT accumulator MUL_C), lambda=4, and
   the four distinct altfmt_A/altfmt_B combinations.  */
/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541128\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4323455642275676360\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3746994889972252872\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4035225266123964616\n} 1 } } */
