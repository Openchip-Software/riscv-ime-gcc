/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb -mabi=lp64d -O2" } */

/* Regression test for the lambda-truncation bug.

   For VLEN values where VLEN/(SEW*MUL_C) is not a perfect-square power
   of 2, no integer lambda exists that satisfies the geometry.
   Previously the runtime sequence computed lambda^2 = VLEN/(SEW*MUL_C),
   then ctz(lambda^2) >> 1 silently truncated to a smaller valid lambda.
   The WARL probe would pass (the rounded lambda is a valid hardware
   lambda) but the implied MUL_C would not match the accumulator type.

   The fix adds a perfect-square check: log2(lambda^2) & 1 == 0.  If
   lambda^2 has an odd log2 (not a perfect square), the code traps
   before reaching vsetvl.

   Verify the check is emitted: the sequence must contain the parity
   test and a trap arm reachable from it.  */

#include <stddef.h>
#pragma riscv intrinsic "vector"

vfloat32m2_t
test_m2 (vfloat32m2_t acc, vfloat32m1_t a, vfloat32m1_t b, size_t vl)
{
  return __riscv_vfmmacc_vv_f32m2 (acc, a, b, vl);
}

/* Verify the parity check is emitted before the WARL probe.  We should
   see ctz (log2(lambda^2)), andi ..., 1 (low-bit parity), and at least
   one ebreak reachable from the odd-parity branch.  GCC typically
   merges the two trap arms (parity failure and WARL mismatch) into a
   single shared ebreak -- both predicates branch to the same trap
   label -- so scan for >= 1 ebreak.  */
/* { dg-final { scan-assembler-times {\tctz\t} 1 } } */
/* { dg-final { scan-assembler-times {\tandi\t[^,]+,[^,]+,1\n} 1 } } */
/* { dg-final { scan-assembler {\tebreak\n} } } */
