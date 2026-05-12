/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Commit 5 extends `classify_lambda_effect' to recover DEFINED(N)
   from a previously-emitted static-VLEN `@vsetvl_ime<mode>' by
   decoding the lambda field of its const_int vtype operand.

   This closes the loop on chained mmaccs: two mmaccs in the same
   basic block with identical (SEW, MUL_C) geometry now share a
   single auto-emitted vsetvl_ime.  The first mmacc triggers the
   auto-emission; when the elision walk runs for the second mmacc,
   it finds the auto-emission's DEFINED(N) and skips re-emission.

   At VLEN=512, SEW=32, MUL_C=1 both mmaccs demand lambda=4.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

vint32m1_t f (vint32m1_t acc, vint32m1_t a, vint32m1_t b, size_t vl)
{
  acc = __riscv_vmmacc_vv_i32m1 (acc, a, b, vl);
  acc = __riscv_vmmacc_vv_i32m1 (acc, a, b, vl);
  return acc;
}

/* Exactly one auto-emitted vsetvl_ime (`li tN,<const>; vsetvl tN, ...').
   The second mmacc inherits the lambda from the first.  */
/* { dg-final { scan-assembler-times {vsetvl\t} 1 } } */

/* Both mmaccs survive.  */
/* { dg-final { scan-assembler-times {vmmacc\.vv} 2 } } */
