/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma -mabi=lp64d -O2" } */

/* Without -mrvv-vector-bits=zvl, VLEN is not statically known and
   the runtime-lambda path is not yet implemented: expect a sorry
   diagnostic rather than silently emitting wrong code.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint32m1_t
f (vint32m1_t vd, vint32m1_t a, vint32m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m1 (vd, a, b, vl); /* { dg-excess-errors "sorry, unimplemented: IME intrinsic with runtime 'lambda'" } */
}
