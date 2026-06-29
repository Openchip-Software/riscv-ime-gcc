/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin_zvvmtls -mabi=lp64d -O3" } */

/* Regression test for the Phase-4 IME-consumer skip in the vsetvl pass.
   When an IME consumer (mmacc) is preceded in the same basic block by a
   basic vsetvl that wrote VL with a different AVL than the consumer
   needs (here the loads use `vl_loads' while the consumer uses
   `vl_mmacc'), Phase 4 must NOT elide the consumer's own vsetvl --
   doing so would leave VL at `vl_loads' and the mmacc would operate on
   the wrong number of elements.

   The bug was visible briefly between
     78ae28b "riscv: hoist runtime IME probe to function entry; teach
              vsetvl pass WARL preservation"
   and its follow-up: the skip checked geometry + IME tuple but did NOT
   check AVL, AND it used `m_vsetvl_def_in[bb]' (state at BB entry)
   rather than the state at the consumer's position within the BB.  The
   follow-up adds the avl_available_p check AND restricts the skip to
   firing only when no prior local_info in the BB has emitted a vsetvl.

   The check below: the function must contain at least one vsetvl
   between the second `vmtl' and the `vfwmmacc' to establish VL from
   `vl_mmacc'.  We match the AVL change pattern: a vsetvl whose AVL
   operand is `a4' (the second function argument register holding
   `vl_mmacc' for lp64d) appearing AFTER the vmtl pair.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

void mismatch (const void *a, const void *b,
	       vfloat32m4_t *acc_p,
	       size_t vl_loads, size_t vl_mmacc)
{
  vfloat32m4_t acc = *acc_p;
  vfloat16m1_t x = __riscv_vmtl_v_f16m1_as_e32 (a, 32, vl_loads);
  vfloat16m1_t y = __riscv_vmtl_v_f16m1_as_e32 (b, 32, vl_loads);
  acc = __riscv_vfwmmacc_vv_f32m4_f16m1 (acc, x, y, vl_mmacc);
  *acc_p = acc;
}

/* The codegen must restore VL=vl_mmacc (a4) before the mmacc.  Match a
   vsetvl with operand `a4' that follows the second vmtl.  */
/* { dg-final { scan-assembler {vmtl\.v[^\n]*\n[^\n]*vmtl\.v[^\n]*\n[^\n]*\n*[^\n]*vsetvl[i]?[^\n]*a4} } } */
