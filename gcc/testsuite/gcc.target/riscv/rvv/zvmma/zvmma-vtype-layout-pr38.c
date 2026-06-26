/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Positive test for the IME spec PR #38 vtype bit layout.  Verifies that
   the compiler emits vtype constants with `bs', `altfmt_A', and
   `altfmt_B' at their NEW positions (XLEN-5 / XLEN-6 / XLEN-7
   respectively, on RV64 = bits 59 / 58 / 57) rather than the OLD
   layout (XLEN-7 / XLEN-6 / XLEN-5 = bits 57 / 58 / 59).

   The companion vtype-constant updates in commit
   "riscv: testsuite: refresh zvmma scan-asm patterns" rebase the
   existing tests' expected constants onto the NEW layout, but a future
   regression that reverted the bit positions would silently make those
   updated tests pass against OLD-layout codegen again (the same
   constants just collide back with the old semantics).  This test
   anchors the NEW layout positively: each expected constant has the
   altfmt / bs bit at its NEW position, so a regression that reverted
   PR #38 would silently emit OLD-layout constants and this test would
   fail.

   Config: VLEN=512 (-zvl512b), SEW=16 (vwmmacc input is e8 -> e16 acc),
   MUL_C=2 (m2 acc), giving lambda^2 = VLEN/(SEW*MUL_C) = 512/32 = 16,
   lambda = 4, encoded as `011' in the lambda[2:0] field = bits 60+61
   set (low 3 bits of the encoded lambda value `log2(lambda) + 1' = 3).

   For each `vwmmacc' sign combination the vsetvl pass emits a
   constant of the shape:

      0x30000000_000000C8                ; base = lambda(011) + e16 + ta + ma + m1
        ^^                                       ; bit 60+61 = lambda enc
        ; bits 59 / 58 / 57: bs / altfmt_A / altfmt_B
        ; bit 6,7: vta=1, vma=1
        ; bit 3-5: vsew = 001 (e16) => bit 3 set
        ; bit 0-2: vlmul = 0 (m1)

   Each variant adds the right altfmt bits at NEW positions:

   - ss (vmmacc_s, signed*signed):    altfmt_A=0, altfmt_B=0
     => base = 3458764513820541128
   - su (signed*unsigned):            altfmt_A=0, altfmt_B=1 (bit 57)
     => base + 2^57 = 3602879701896397000
   - us (unsigned*signed):            altfmt_A=1 (bit 58), altfmt_B=0
     => base + 2^58 = 3746994889972252872
   - uu (unsigned*unsigned):          altfmt_A=1, altfmt_B=1
     => base + 2^57 + 2^58 = 3891110078048108744

   A regression that reverted PR #38 would emit these constants with
   altfmt_A at bit 59 and altfmt_B at bit 58, giving the OLD set
   { 3458764513820541128, 3746994889972252872, 4035225266123964616,
     4323455642275676360 } and this test would fail because the NEW
   constants are missing.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint16m2_t
ss (vint16m2_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2 (vd, a, b, vl);
}

vint16m2_t
su (vint16m2_t vd, vint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2_su (vd, a, b, vl);
}

vint16m2_t
us (vint16m2_t vd, vuint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16m2_us (vd, a, b, vl);
}

vuint16m2_t
uu (vuint16m2_t vd, vuint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_u16m2 (vd, a, b, vl);
}

/* NEW-layout constants (bs / altfmt_A / altfmt_B at XLEN-5 / XLEN-6 /
   XLEN-7).  Each appears exactly once across the four functions.  */
/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541128\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3602879701896397000\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3746994889972252872\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3891110078048108744\n} 1 } } */

/* OLD-layout constants MUST NOT appear -- a regression to PR #38 would
   surface here.  */
/* { dg-final { scan-assembler-not {\tli\tt0,4035225266123964616\n} } } */
/* { dg-final { scan-assembler-not {\tli\tt0,4323455642275676360\n} } } */
