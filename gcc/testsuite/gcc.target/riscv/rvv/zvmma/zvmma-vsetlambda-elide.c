/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* At VLEN=512, SEW=32, MUL_C=1: lambda^2 = 16 -> lambda=4.
   `__riscv_vsetlambda(4)' changes only the lambda field of vtype, so
   the multiply-accumulate still needs its IME vsetvl to set vl, SEW,
   LMUL and the format bits (0x30000000000000d0: lambda=4, e32, m1, ta,
   ma) from its own AVL.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

vint32m1_t test (vint32m1_t acc, vint32m1_t a, vint32m1_t b, size_t vl)
{
  __riscv_vsetlambda (4);
  return __riscv_vmmacc_vv_i32m1 (acc, a, b, vl);
}

/* The user's lambda install and the IME vsetvl.  */
/* { dg-final { scan-assembler-times {vsetvl\t} 2 } } */
/* { dg-final { scan-assembler-times {li\tt0,3458764513820541136\n\tvsetvl\tt0,a0,t0\n\tvmmacc\.vv} 1 } } */
