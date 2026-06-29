/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin_zvvmtls -mabi=lp64d -O3" } */

/* Documentation test for two IME consumers in the same function with
   the same vl argument.

   The current (pre-probe-dedup) state emits one probe per
   `maybe_emit_ime_runtime_vtype' call -- i.e. one per IME consumer.
   This test asserts that fact so that when probe-dedup lands (option
   F from the gap-2 review), the test will need to be updated and the
   change is visible in code review.

   Specifically: count the number of `vsetvl_ime_reg' emissions at
   function entry.  Each one is identifiable by a `vsetvl <rd>, <avl>,
   <full_vtype_reg>' immediately followed by a `csrr ...,vtype' (the
   WARL readback).  With two consumers + no dedup, expect two such
   sequences.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

void two_consumers_same_vl (const void *a, const void *b,
			    vfloat32m4_t *acc_p, size_t vl)
{
  vfloat32m4_t acc = *acc_p;
  vfloat16m1_t x1 = __riscv_vmtl_v_f16m1_as_e32 (a, 32, vl);
  vfloat16m1_t y1 = __riscv_vmtl_v_f16m1_as_e32 (b, 32, vl);
  acc = __riscv_vfwmmacc_vv_f32m4_f16m1 (acc, x1, y1, vl);

  vfloat16m1_t x2 = __riscv_vmtl_v_f16m1_as_e32 (a, 32, vl);
  vfloat16m1_t y2 = __riscv_vmtl_v_f16m1_as_e32 (b, 32, vl);
  acc = __riscv_vfwmmacc_vv_f32m4_f16m1 (acc, x2, y2, vl);

  *acc_p = acc;
}

/* The mmacc demand is satisfied by upstream IME state (entry probe +
   WARL preservation through any basic vsetvli between consumers); no
   extra IME vsetvl is emitted right before either mmacc.  Match: two
   `vfwmmacc.vv' instructions, NEITHER preceded by an immediate
   register-form `vsetvl <rd>, <avl>, <reg>'.  */
/* { dg-final { scan-assembler-times {vfwmmacc\.vv} 2 } } */
/* { dg-final { scan-assembler-not {vsetvl\s+[at][0-9]+,[at][0-9]+,[at][0-9]+[^\n]*\n[^\n]*vfwmmacc\.vv} } } */
