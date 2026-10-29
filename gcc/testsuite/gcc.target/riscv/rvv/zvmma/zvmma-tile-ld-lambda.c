/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* A tile load without `_L{N}' takes lambda from vtype.  When it feeds a
   multiply-accumulate and no lambda is established, it must run with
   the lambda of that multiply-accumulate: at VLEN=256, SEW=32,
   MUL_C=2 that is lambda=2 (0x20000000000000d0: lambda=2, e32, m1, ta,
   ma), set before the load rather than after it.  */

typedef __SIZE_TYPE__ size_t;
typedef long ptrdiff_t;
#pragma riscv intrinsic "vector"

vint32m2_t
a2 (const int *p, ptrdiff_t ld, size_t vl, vint32m2_t vd, vint32m1_t b)
{
  vint32m1_t a = __riscv_vmtl_v_i32m1 (p, ld, vl);
  return __riscv_vmmacc_vv_i32m2 (vd, a, b, vl);
}

/* { dg-final { scan-assembler {li\tt0,2305843009213694160\n\tvsetvl\tt0,a2,t0\n\tvmtl\.v} } } */
/* { dg-final { scan-assembler-not {vsetvli\t[a-z0-9]+,a2,e32,m1} } } */
