/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Commit 4 extends the elision in commit 3 with a CFG-level lambda
   dataflow.  Here the user establishes lambda=4 in the entry block;
   the consumer (vmmacc) lives in a loop body successor block.  The
   cross-block dataflow propagates DEFINED(2) (=log2(4)) to the loop
   entry, and the consumer's auto-vsetvl_ime is elided despite living
   in a different basic block from the user's setter.

   At VLEN=512, SEW=32, MUL_C=1 the mmacc demands lambda=4 (matching
   the user's setting).  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

vint32m1_t loop_acc (vint32m1_t acc, vint32m1_t a, vint32m1_t b,
		     size_t vl, int n)
{
  __riscv_vsetlambda (4);
  for (int i = 0; i < n; i++)
    acc = __riscv_vmmacc_vv_i32m1 (acc, a, b, vl);
  return acc;
}

/* Exactly one `vsetvl' in the function: the user's install in the
   entry block.  The loop body's mmacc would normally trigger an
   auto-vsetvl_ime; with cross-block dataflow it sees DEFINED(2)
   at block entry and skips emission.  */
/* { dg-final { scan-assembler-times {vsetvl\t} 1 } } */

/* No auto-emitted IME vsetvl (the canonical "li tN,<const>; vsetvl
   tN,..." sequence) inside the loop body.  */
/* { dg-final { scan-assembler-not {li\tt0,[0-9]+\n\tvsetvl\tt0,} } } */
