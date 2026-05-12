/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zbb -mabi=lp64d -O2" } */

/* Runtime-arg form of `__riscv_vsetlambda'.  The expansion is:

     beq  arg, x0, .Lread     ; skip write if arg == 0
     ctz  enc, arg            ; encode arg -> 3-bit lambda field
     addi enc, enc, 1
     csrr t,  vtype
     ... RMW ...
     vsetvl x0, x0, t         ; install
   .Lread:
     csrr t, vtype            ; readback
     ... decode ...

   Requires Zbb (for `ctz').  Without Zbb the expander emits a
   sorry() and the user is expected to dispatch through a constant
   switch (the ime-prelude's `lambda_set_v' does exactly that).  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

size_t f (size_t v)
{
  return __riscv_vsetlambda (v);
}

/* One vsetvl install on the write side.  */
/* { dg-final { scan-assembler-times {vsetvl\tx0,x0,} 1 } } */
/* The runtime encoding uses Zbb's ctz.  */
/* { dg-final { scan-assembler-times {ctz\t} 1 } } */
/* A branch on zero skips the write path.  */
/* { dg-final { scan-assembler {beq\t} } } */
