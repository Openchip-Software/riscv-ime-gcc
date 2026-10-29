/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma -mabi=lp64d -O2" } */

/* A zero leading dimension selects the natural dimension; it is passed
   in x0 rather than in a register loaded with zero.  */

#include <stddef.h>
#pragma riscv intrinsic "vector"

vint32m1_t
ld0 (const int *p, size_t vl)
{
  return __riscv_vmtl_v_i32m1 (p, 0, vl);
}

vint32m1_t
tld0 (const int *p, size_t vl)
{
  return __riscv_vmttl_v_i32m1 (p, 0, vl);
}

void
st0 (int *p, vint32m1_t v, size_t vl)
{
  __riscv_vmts_v_i32m1 (p, 0, v, vl);
}

void
tst0 (int *p, vint32m1_t v, size_t vl)
{
  __riscv_vmtts_v_i32m1 (p, 0, v, vl);
}

vint32m1_t
ld (const int *p, long ld, size_t vl)
{
  return __riscv_vmtl_v_i32m1 (p, ld, vl);
}

/* { dg-final { scan-assembler-times {\mvmtl\.v\tv[0-9]+,0\(a[0-9]\),zero\M} 1 } } */
/* { dg-final { scan-assembler-times {\mvmttl\.v\tv[0-9]+,0\(a[0-9]\),zero\M} 1 } } */
/* { dg-final { scan-assembler-times {\mvmts\.v\tv[0-9]+,0\(a[0-9]\),zero\M} 1 } } */
/* { dg-final { scan-assembler-times {\mvmtts\.v\tv[0-9]+,0\(a[0-9]\),zero\M} 1 } } */
/* { dg-final { scan-assembler-times {\mvmtl\.v\tv[0-9]+,0\(a[0-9]\),a[0-9]\M} 1 } } */
/* { dg-final { scan-assembler-not {\mli\t[a-z0-9]+,0\M} } } */
