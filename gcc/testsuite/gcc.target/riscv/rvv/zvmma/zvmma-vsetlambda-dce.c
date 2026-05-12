/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm -mabi=lp64d -O2" } */

/* Commit 4 introduces a CFG-level lambda dataflow that DCE's
   redundant `__riscv_vsetlambda(N)' installs: an install whose target
   value is already established at its insertion point on every
   predecessor path is dead.

   This function performs two consecutive vsetlambda(4) calls.  The
   second is dead -- the lattice arriving at it is DEFINED(2) (log2
   of lambda=4) and the install's encoded value matches.  */

#pragma riscv intrinsic "vector"

void f (void)
{
  __riscv_vsetlambda (4);
  __riscv_vsetlambda (4);    /* dead */
}

/* Exactly one `vsetvl x0, x0, ...' remains after DCE.  */
/* { dg-final { scan-assembler-times {vsetvl\tx0,x0,} 1 } } */
