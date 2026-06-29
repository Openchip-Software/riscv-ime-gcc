/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin_zvvmtls -mabi=lp64d -O3" } */

/* Regression test for the runtime IME probe emission point.

   Commit 78ae28b "riscv: hoist runtime IME probe to function entry;
   teach vsetvl pass WARL preservation" moved the probe (lambda compute,
   perfect-square check, vsetvl_ime_reg, WARL readback) from each
   consumer's expansion point to function entry.  This test verifies
   the probe lands at function entry, not inside the loop body.

   The codegen invariants:
     - The lambda computation (`csrr vlenb' followed by shifts and
       `ctz') must appear OUTSIDE the loop label.  Either it's emitted
       before the loop's first label, or inside a function prologue.
     - The loop body itself must NOT contain a perfect-square trap
       (`bne ..., zero, .Lebreak') or a WARL readback (`csrr vtype').
     - The loop body must NOT contain a `vsetvl' (register-form, IME)
       instruction.

   Together these confirm the W=2 single-K zero-vsetvl-in-loop shape
   the optimisation is designed to produce.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

void loop_zero_vsetvl (const void *a_base, const void *b_base,
		      vfloat32m4_t *acc_p, size_t n, size_t vl)
{
  vfloat32m4_t acc = *acc_p;
  for (size_t i = 0; i < n; i++)
    {
      vfloat16m1_t a = __riscv_vmtl_v_f16m1_as_e32 ((const char *)a_base + i * 128, 32, vl);
      vfloat16m1_t b = __riscv_vmtl_v_f16m1_as_e32 ((const char *)b_base + i * 128, 32, vl);
      acc = __riscv_vfwmmacc_vv_f32m4_f16m1 (acc, a, b, vl);
    }
  *acc_p = acc;
}

/* Probe is at function entry: the csrr vlenb appears before any
   loop label.  */
/* { dg-final { scan-assembler {csrr\s+[at][0-9]+,vlenb[^\n]*\n(?:[^.][^\n]*\n)*\.L} } } */

/* Loop body has no register-form vsetvl (rd != zero), which would be a
   probe-style emission inside the loop.  Match the .L*: label that
   starts the loop body and assert no `vsetvl' (register-form) appears
   between it and the back-edge.  */
/* { dg-final { scan-assembler-not {\.L[0-9]+:[^.]*\n(?:(?!\.L)(?!ret).)*\n\s*vsetvl\s+[at][0-9]+,} } } */
