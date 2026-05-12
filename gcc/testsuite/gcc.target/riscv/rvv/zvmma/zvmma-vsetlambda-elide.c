/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* At VLEN=512, SEW=32, MUL_C=1: lambda^2 = 16 -> lambda=4.
   The user's `__riscv_vsetlambda(4)' establishes that lambda; the
   mmacc auto-vsetvl_ime would re-establish the same lambda and is
   therefore redundant -- commit 3 elides it.

   Expected codegen for `test':
     - one csrr + RMW + `vsetvl x0,x0,...' (the user vsetlambda),
     - then `vmmacc.vv ...' immediately (NO auto-vsetvl_ime),
     - then ret.

   Pinning: a flag-on build must NOT emit a `li tN, <const>; vsetvl
   tN, ...' pair (the canonical asm template for `@vsetvl_ime<mode>'
   at -O2).  We assert a hard upper bound on `vsetvl' emissions
   (exactly one: the user's), and assert no `li ... ; vsetvl' on
   the same scratch register.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

vint32m1_t test (vint32m1_t acc, vint32m1_t a, vint32m1_t b, size_t vl)
{
  __riscv_vsetlambda (4);
  return __riscv_vmmacc_vv_i32m1 (acc, a, b, vl);
}

/* Exactly one vsetvl in the function (the user's install).  */
/* { dg-final { scan-assembler-times {vsetvl\t} 1 } } */
/* No auto-emitted IME vsetvl ("li t0,<const>\n\tvsetvl t0,...") -- the
   user already established the right lambda.  */
/* { dg-final { scan-assembler-not {li\tt0,[0-9]+\n\tvsetvl\tt0,} } } */
