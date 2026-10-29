/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The user establishes lambda=4 in the entry block and the
   multiply-accumulate is in a loop.  At VLEN=512, SEW=32, MUL_C=1 it
   demands lambda=4 too, but it still needs its IME vsetvl for vl, SEW,
   LMUL and the format bits; that vsetvl belongs before the loop, not
   in it.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

vint32m1_t loop_acc (vint32m1_t acc, vint32m1_t a, vint32m1_t b,
		     size_t vl, int n)
{
  __riscv_vsetlambda (4);
  for (int i = 0; i < n; i++)
    acc = __riscv_vmmacc_vv_i32m1 (acc, a, b, vl);
  return acc;
}

/* The user's lambda install and one IME vsetvl, outside the loop.  */
/* { dg-final { scan-assembler-times {vsetvl\t} 2 } } */
/* { dg-final { scan-assembler-times {li\tt0,3458764513820541136\n\tvsetvl\tt0,a0,t0\n\.L[0-9]+:} 1 } } */
