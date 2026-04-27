/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* W=1 mixed-sign vmmacc (_su / _us).  All four sign combinations share
   the signed accumulator (spec mandates a signed accumulator for W=1
   mmacc); only the input signedness varies between the _s, _u, _su
   and _us variants.  At zvl256b: SEW=8 MUL_C=2 yields lambda=2 and
   SEW=32 MUL_C=2 yields lambda=1 (both valid perfect-square parity);
   SEW=64 MUL_C=4 yields lambda=1.  */

/* Default source LMUL=1.  Cover SEW=8/32/64 accumulators.  */
vint8m2_t
mmacc_su_i8m2 (vint8m2_t vd, vint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i8m2_su (vd, a, b, vl);
}

vint8m2_t
mmacc_us_i8m2 (vint8m2_t vd, vuint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i8m2_us (vd, a, b, vl);
}

vint32m2_t
mmacc_su_i32m2 (vint32m2_t vd, vint32m1_t a, vuint32m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_su (vd, a, b, vl);
}

vint32m2_t
mmacc_us_i32m2 (vint32m2_t vd, vuint32m1_t a, vint32m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_us (vd, a, b, vl);
}

vint64m4_t
mmacc_su_i64m4 (vint64m4_t vd, vint64m1_t a, vuint64m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i64m4_su (vd, a, b, vl);
}

vint64m4_t
mmacc_us_i64m4 (vint64m4_t vd, vuint64m1_t a, vint64m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i64m4_us (vd, a, b, vl);
}

/* _lm{2,4,8} variants at the i32m2 accumulator.  */
vint32m2_t
mmacc_su_i32m2_lm2 (vint32m2_t vd, vint32m2_t a, vuint32m2_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_su_lm2 (vd, a, b, vl);
}

vint32m2_t
mmacc_us_i32m2_lm2 (vint32m2_t vd, vuint32m2_t a, vint32m2_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_us_lm2 (vd, a, b, vl);
}

vint32m2_t
mmacc_su_i32m2_lm4 (vint32m2_t vd, vint32m4_t a, vuint32m4_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_su_lm4 (vd, a, b, vl);
}

vint32m2_t
mmacc_us_i32m2_lm4 (vint32m2_t vd, vuint32m4_t a, vint32m4_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_us_lm4 (vd, a, b, vl);
}

vint32m2_t
mmacc_su_i32m2_lm8 (vint32m2_t vd, vint32m8_t a, vuint32m8_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_su_lm8 (vd, a, b, vl);
}

vint32m2_t
mmacc_us_i32m2_lm8 (vint32m2_t vd, vuint32m8_t a, vint32m8_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m2_us_lm8 (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvmmacc\.vv\t} 12 } } */
