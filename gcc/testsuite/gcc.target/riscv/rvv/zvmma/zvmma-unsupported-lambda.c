/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* At VLEN=256, SEW=32, MUL_C=1 the required lambda^2 would be 8,
   which has no integer square root -- no supported lambda exists
   for this (VLEN, SEW, MUL_C) combination.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint32m1_t
f (vint32m1_t vd, vint32m1_t a, vint32m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m1 (vd, a, b, vl); /* { dg-excess-errors "sorry, unimplemented: IME intrinsic: the '\\(VLEN, SEW, MUL_C\\)' combination" } */
}
