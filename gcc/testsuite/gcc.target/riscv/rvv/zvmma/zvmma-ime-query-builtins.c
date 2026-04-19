/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b_zbb -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Test __riscv_ime_vlen() and __riscv_ime_lambda() geometry queries.

   At zvl256b with -mrvv-vector-bits=zvl:
     vlenb         = 32  (32 bytes = 256 bits)
     __riscv_ime_vlen()   = 256 (VLEN in bits)
     __riscv_ime_lambda() = 4   (default SEW=8 query:
                                 ctz(vlenb=32) = 5, lambda = 1<<(5/2) = 4)

   When VLEN is compile-time known, both fold to constants.  */

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

/* Both should fold to li instructions with no csrr/ctz.  */
/* { dg-final { scan-assembler-times {li\t[a-z0-9]+,256} 1 } } */
/* { dg-final { scan-assembler-times {li\t[a-z0-9]+,4} 1 } } */
/* { dg-final { scan-assembler-not {csrr} } } */
/* { dg-final { scan-assembler-not {ctz} } } */
