/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvfh_zvmma_zvvmm_zvvmtls -mabi=lp64d -O2" } */

/* Regression test for the λ-USE modelling bug.

   Before the fix, the vsetvl pass treated `riscv_vsetlambda_install'
   as an opaque vtype writer (set_unknown), broke the SEW/LMUL demand
   chain, then re-inserted a plain `vsetvli' before the IME tile op
   to re-establish (SEW, LMUL).  That re-inserted vsetvli is a may-DEF
   of vtype.lambda under WARL and silently clobbered the user's
   `__riscv_vsetlambda(N)' setting.

   After the fix:
     - The install is transparent in the SEW/LMUL/policy/AVL demand
       analysis (it only modifies vtype.lambda).
     - The lambda lattice classifies IME tile load/store and mmacc
       insns as USE_ONLY of vtype.lambda.
     - When the pass would insert a vsetvli before a lambda-using
       consumer and a static lambda DEF reaches that point, it emits
       a register-form `vsetvl_ime' that programs the full vtype
       (SEW, LMUL, ta, ma, λ) instead of a plain vsetvli.  The user's
       λ is preserved in a single atomic vtype write.  */

typedef unsigned long size_t;
typedef long ptrdiff_t;
#pragma riscv intrinsic "vector"

void f (_Float16 *A, _Float16 *B, ptrdiff_t lda, size_t vl)
{
  size_t actual = __riscv_vsetvl_e16m1 (vl);
  __riscv_vsetlambda (4);
  vfloat16m1_t a = __riscv_vmtl_v_f16m1 (A, lda, actual);
  __riscv_vmts_v_f16m1 (B, lda, a, actual);
}

/* The pass must NOT emit a plain `vsetvli zero, ..., e16, m1, ...'
   between the install and the tile load -- that would WARL-clobber
   λ.  Instead it emits a register-form `vsetvl' from a const-loaded
   full vtype (the canonical "li tN, <big const>; vsetvl tN, ..."
   sequence).  Match the literal "li tN, <number>" preceding a
   "vsetvl" to confirm that path was taken.  */
/* { dg-final { scan-assembler {li\tt0,[0-9]+\n\tvsetvl\tt0,} } } */
