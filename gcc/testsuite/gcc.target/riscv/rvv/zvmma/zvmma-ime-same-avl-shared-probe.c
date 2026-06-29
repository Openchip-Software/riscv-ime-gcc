/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin_zvvmtls -mabi=lp64d -O3" } */

/* Test for the per-function probe deduplication.

   Two IME consumers with the same IME geometry (sew=32, mul_c=m4,
   input LMUL=m1, widen_w=2, etc.) share a single runtime probe at
   function entry, regardless of whether they share the same `vl'
   argument.  The probe-dedup cache in `machine_function' keys on the
   packed IME tuple (AVL is not part of the key), so a second
   consumer with the same tuple reuses the cached `full_vtype' pseudo
   and `emit_ime_runtime_vtype_setup' emits no fresh probe.

   This test exercises the simpler case where the vl argument is also
   shared.  The codegen invariants:
     - Exactly one `csrr <reg>,vlenb' (the lambda computation that
       starts the probe sequence).
     - Two `vfwmmacc.vv' consumers.
     - Neither vfwmmacc is preceded by a register-form
       `vsetvl <rd>, <avl>, <reg>' -- the entry probe + WARL
       preservation covers both.  */

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

/* Exactly one probe (one `csrr ...,vlenb' lambda-compute) for two
   consumers.  Dedup hit on the second `maybe_emit_ime_runtime_vtype'
   call.  */
/* { dg-final { scan-assembler-times {csrr\s+[at][0-9]+,vlenb} 1 } } */

/* The mmacc demand is satisfied by upstream IME state (entry probe +
   WARL preservation through any basic vsetvli between consumers); no
   extra IME vsetvl is emitted right before either mmacc.  Match: two
   `vfwmmacc.vv' instructions, NEITHER preceded by an immediate
   register-form `vsetvl <rd>, <avl>, <reg>'.  */
/* { dg-final { scan-assembler-times {vfwmmacc\.vv} 2 } } */
/* { dg-final { scan-assembler-not {vsetvl\s+[at][0-9]+,[at][0-9]+,[at][0-9]+[^\n]*\n[^\n]*vfwmmacc\.vv} } } */
