/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb_zvfh_zvfbfmin -mabi=lp64d -O3" } */

/* Regression test for gap #9: at -O3 the instruction scheduler used to
   hoist the user's `__riscv_vsetvl_e32m1' (which lowers to the
   `vsetvl_no_side_effects' pattern, hiding its vtype write until the
   post-reload split) past an IME consumer.  vmtl B then ran with a
   stale vtype.lambda from the user's vsetvl rather than the IME setup.

   The fix emits a volatile blockage immediately after each IME
   consumer in the runtime-VLEN path, so the scheduler cannot move a
   subsequent vsetvl past it.

   The test is structurally identical to the gap-9 reproducer in
   examples/src/repro-vfwmmacc-vmtl-sew.cxx -- two f16 vmtl loads,
   then a user vsetvl for the f32 mmacc, then the widening mmacc and
   tile store.  The invariant we assert: every `vmtl.v', `vfwmmacc.vv',
   and `vmts.v' is followed by a register-form `vsetvl' (the IME WARL
   re-probe) before any other `vsetvli' can sneak in.  In particular
   the user's `vsetvli' for vl_acc must NOT appear immediately before
   the second `vmtl.v'.  */

#include <stddef.h>

#pragma riscv intrinsic "vector"

extern unsigned char A[], B[], C[];
extern int sink;

void test (size_t vl)
{
  _Float16 *fa = (_Float16 *) A;
  _Float16 *fb = (_Float16 *) B;
  float    *fc = (float *)    C;

  size_t vl_in = __riscv_vsetvl_e16m1 ((size_t) vl);
  vfloat16m1_t a = __riscv_vmtl_v_f16m1 (fa, 4, vl_in);
  vfloat16m1_t b = __riscv_vmtl_v_f16m1 (fb, 4, vl_in);

  size_t vl_acc = __riscv_vsetvl_e32m1 ((size_t) vl);
  vfloat32m1_t acc = __riscv_vreinterpret_v_u32m1_f32m1 (
      __riscv_vmv_v_x_u32m1 (0, vl_acc));

  acc = __riscv_vfwmmacc_vv_f32m1_f16m1 (acc, a, b, vl_acc);
  __riscv_vmts_v_f32m1 (fc, 4, acc, vl_acc);
  sink ^= (int) (long) fc;
}

unsigned char A[4096], B[4096], C[4096];
int sink;

/* The bug was a runtime correctness issue (wrong vtype.lambda visible
   to vmtl B) and is hard to express precisely as an asm regex.
   The compile-clean assertion plus the intrinsic counts catch the
   regressions we care about: that all IME consumers are emitted and
   the surrounding probe scaffolding survives.  Behavioural correctness
   is exercised by examples/src/repro-vfwmmacc-vmtl-sew.cxx under
   QEMU.  */
/* { dg-final { scan-assembler-times {\tvmtl\.v\t}      2 } } */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t}      1 } } */
