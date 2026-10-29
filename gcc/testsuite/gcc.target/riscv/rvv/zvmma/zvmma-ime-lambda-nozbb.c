/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* __riscv_ime_lambda() reads vtype.lambda and needs no Zbb.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

size_t get_lambda (void)
{
  return __riscv_ime_lambda ();
}

/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vtype} 1 } } */
/* { dg-final { scan-assembler-not {ctz\t} } } */
