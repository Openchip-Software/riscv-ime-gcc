/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb -mabi=lp64d -O2" } */

/* Test __riscv_ime_vlen() and __riscv_ime_lambda() when VLEN is not
   known at compile time (no -mrvv-vector-bits=zvl).  */

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

/* __riscv_ime_vlen(): csrr vlenb + slli by 3.  */
/* __riscv_ime_lambda(): csrr vtype + decode of the lambda field.  */
/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vlenb} 1 } } */
/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vtype} 1 } } */
/* { dg-final { scan-assembler-not {ctz\t} } } */
