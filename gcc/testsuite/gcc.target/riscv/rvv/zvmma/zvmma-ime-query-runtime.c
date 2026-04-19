/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb -mabi=lp64d -O2" } */

/* Test that __riscv_ime_vlen() and __riscv_ime_lambda() emit the
   runtime compute sequence when VLEN is not statically known.
   Note: no -mrvv-vector-bits=zvl, so VLEN is poly_int.  */

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
/* __riscv_ime_lambda(): csrr vlenb + ctz + srli + sll.  */
/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vlenb} 2 } } */
/* { dg-final { scan-assembler-times {ctz\t} 1 } } */
