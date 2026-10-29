/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b_zbb -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Test __riscv_ime_vlen() and __riscv_ime_lambda() at a compile-time
   VLEN of 256.  __riscv_ime_vlen() folds to 256.  __riscv_ime_lambda()
   returns the lambda currently selected in vtype, which VLEN does not
   determine, so it reads and decodes vtype.lambda.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

size_t get_vlen (void)
{
  return __riscv_ime_vlen ();
}

size_t get_lambda (void)
{
  return __riscv_ime_lambda ();
}

/* { dg-final { scan-assembler-times {li\t[a-z0-9]+,256} 1 } } */
/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vtype} 1 } } */
/* { dg-final { scan-assembler-not {csrr\t[a-z0-9]+,vlenb} } } */
