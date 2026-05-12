/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm -mabi=lp64d -O2" } */

/* Discarded-return calls to `__riscv_vsetlambda' must NOT be DCE'd by
   the middle-end -- they have an observable side effect on vtype.
   This test pins the call_properties = CP_READ_CSR | CP_WRITE_CSR
   annotation on the function_base; without it, gimple DCE removes
   the unused-return calls and only the readback for the last one
   survives.  */

#pragma riscv intrinsic "vector"

void discard (void)
{
  __riscv_vsetlambda (1);
  __riscv_vsetlambda (4);
  __riscv_vsetlambda (16);
}

/* Three writes -> three `vsetvl x0,x0,...' and three csrr+RMW pairs.
   The function body has no return-value readback (return type void).  */
/* { dg-final { scan-assembler-times {vsetvl\tx0,x0,} 3 } } */
/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vtype} 3 } } */
