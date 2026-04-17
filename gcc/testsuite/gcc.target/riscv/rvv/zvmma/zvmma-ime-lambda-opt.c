/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2 -mrvv-ime-lambda=4" } */

/* At VLEN=512, SEW=32, MUL_C=1: lambda^2 = 512/32 = 16, lambda = 4.
   With -mrvv-ime-lambda=4 this must succeed.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint32m1_t
mmacc_ok (vint32m1_t vd, vint32m1_t a, vint32m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m1 (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tvmmacc\.vv\t} 1 } } */
