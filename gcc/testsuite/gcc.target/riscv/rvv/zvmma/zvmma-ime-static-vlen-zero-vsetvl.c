/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin_zvvmtls_zvl128b -mabi=lp64d -O3 -mrvv-vector-bits=zvl" } */

/* Regression test for the static-VLEN IME loop body shape.

   When VLEN is compile-time-known (via `-mrvv-vector-bits=zvl' on a
   `_zvl*b' march), `emit_ime_runtime_vtype_setup' takes the
   static-lambda path -- no runtime probe, no perfect-square check, no
   WARL readback.  The vsetvl pass's IME-VALID recognition (commit
   b77ef98) + the kill-model WARL refinement (commit 78ae28b) +
   the Phase 4 IME-consumer skip (commits 78ae28b, 6aa3f96) together
   collapse the loop body to zero in-loop vsetvls.

   The codegen invariants:
     - One basic `vsetivli' (or vsetvli) at the loop preheader
       establishes the (SEW, LMUL, ta, ma) geometry.
     - The loop body has the loads and the mmacc but NO vsetvl.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

void loop_static_zero (const void *a_base, const void *b_base,
		       vfloat32m4_t *acc_p, size_t n)
{
  size_t vl = __riscv_vsetvl_e32m1 (8);
  vfloat32m4_t acc = *acc_p;
  for (size_t i = 0; i < n; i++)
    {
      vfloat32m1_t a = __riscv_vmtl_v_f32m1_as_e32 ((const char *)a_base + i * 128, 32, vl);
      vfloat32m1_t b = __riscv_vmtl_v_f32m1_as_e32 ((const char *)b_base + i * 128, 32, vl);
      acc = __riscv_vfmmacc_vv_f32m4 (acc, a, b, vl);
    }
  *acc_p = acc;
}

/* Loop body contains the mmacc but no in-loop vsetvl.  Match the
   sequence: `.L<N>:' followed by two `vmtl.v', then `vfmmacc.vv',
   with no `vsetvl' in between.  */
/* { dg-final { scan-assembler {\.L[0-9]+:[^.]*\n[^\n]*vmtl\.v[^\n]*\n[^\n]*vmtl\.v[^\n]*\n(?:[^\n]*\n){0,3}[^\n]*vfmmacc\.vv} } } */

/* Explicit negative: no `vsetvl' (in any form: vsetvl, vsetvli,
   vsetivli) between the second vmtl and the mmacc.  */
/* { dg-final { scan-assembler-not {vmtl\.v[^\n]*\n[^\n]*vmtl\.v[^\n]*\n[^\n]*vseti?vli?\s+[^\n]*\n[^\n]*vfmmacc} } } */
