/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb -mabi=lp64d -O2" } */

/* Without -mrvv-vector-bits=zvl, VLEN is not statically known.
   The runtime-lambda path emits a csrr vlenb + ctz + shifts + vsetvl
   sequence to compute and set the full vtype at runtime.  Verify that
   the key instructions appear in the output.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint32m1_t
f (vint32m1_t vd, vint32m1_t a, vint32m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i32m1 (vd, a, b, vl);
}

/* { dg-final { scan-assembler "csrr\t\[a-z0-9\]+,vlenb" } } */
/* { dg-final { scan-assembler "ctz\t" } } */
/* { dg-final { scan-assembler "vsetvl\t" } } */
/* { dg-final { scan-assembler "vmmacc\\.vv\t" } } */
