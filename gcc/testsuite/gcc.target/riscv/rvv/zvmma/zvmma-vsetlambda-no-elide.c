/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Negative case for the elision optimisation introduced in commit 3:
   when the user pre-sets lambda to a value that does NOT match what
   the mmacc demands, the pass must NOT elide -- the auto-vsetvl_ime
   has to overwrite the user's wrong choice.

   At VLEN=512, SEW=32, MUL_C=1 the mmacc demands lambda=4.  The
   user here sets lambda=2 instead; the auto-emission must run.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

vint32m1_t test (vint32m1_t acc, vint32m1_t a, vint32m1_t b, size_t vl)
{
  __riscv_vsetlambda (2);       /* User wants 2, mmacc demands 4. */
  return __riscv_vmmacc_vv_i32m1 (acc, a, b, vl);
}

/* Two vsetvls in the function: the user's install, then the IME
   pipeline's overriding auto-vsetvl.  */
/* { dg-final { scan-assembler-times {vsetvl\t} 2 } } */
