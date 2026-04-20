/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl2048b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

#include <stddef.h>
#pragma riscv intrinsic "vector"

/* Force the allocator to choose: acc uses m4 (4 regs), a/b use m1.
   With "=&vr" (early-clobber), a/b MUST NOT fall into the acc's
   register range.  Without it, nothing prevents a or b from being
   placed on top of acc's 4-register group.  */
vfloat32m4_t
stress (vfloat32m4_t acc, vfloat32m1_t a, vfloat32m1_t b, size_t vl)
{
  /* Use acc, a, b multiple times in complex dependency to stress allocator */
  vfloat32m4_t r = __riscv_vfmmacc_vv_f32m4 (acc, a, b, vl);
  return r;
}

/* Verify no overlap: if the acc is v8..v11 (m4), then a and b must
   be outside v8..v11.  */
/* { dg-final { scan-assembler-not {vfmmacc\.vv\tv8,v[89],} } } */
/* { dg-final { scan-assembler-not {vfmmacc\.vv\tv8,v1[01],} } } */
/* { dg-final { scan-assembler-not {vfmmacc\.vv\tv8,v[0-9]+,v[89]\b} } } */
/* { dg-final { scan-assembler-not {vfmmacc\.vv\tv8,v[0-9]+,v1[01]\b} } } */
