/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* End-user test for the IME pair / unpair primitives.

   The IME spec forbids m16 in function signatures (see
   `zvmma-m16-arg-return-reject.c').  Users that need to move m16
   data across the call boundary must split it into two m8 halves
   before passing and recompose it with `__riscv_ime_vpair' on the
   receiving side.  The matching teardown uses
   `__riscv_ime_vunpairlo' / `__riscv_ime_vunpairhi' to extract the
   halves before returning.

   `f' below performs the round trip end-to-end: receive two m8
   halves plus the mmacc operands, pair the halves into an m16
   accumulator, run a W=1 mmacc with m8 inputs (`_lm8' source-LMUL
   variant), unpair the result, and combine the halves into the m8
   return value.  No m16 type crosses the call boundary, so the
   arg/return diagnostic must NOT fire.  */

#pragma riscv intrinsic "vector"

typedef unsigned long size_t;

vint32m8_t
f (vint32m8_t lo, vint32m8_t hi, vint32m8_t a, vint32m8_t b, size_t vl)
{
  vint32m16_t acc = __riscv_ime_vpair_i32m16 (lo, hi);
  acc = __riscv_vmmacc_vv_i32m16_lm8 (acc, a, b, vl);
  vint32m8_t r_lo = __riscv_ime_vunpairlo_i32m16 (acc);
  vint32m8_t r_hi = __riscv_ime_vunpairhi_i32m16 (acc);
  return __riscv_vadd_vv_i32m8 (r_lo, r_hi, vl);
}

/* The mmacc must survive.  */
/* { dg-final { scan-assembler-times {vmmacc\.vv} 1 } } */

/* No spec-violation diagnostic.  */
/* { dg-final { scan-assembler-not "sorry" } } */
