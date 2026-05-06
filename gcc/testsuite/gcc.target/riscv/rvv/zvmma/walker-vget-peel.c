/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb_zvfh_zvfbfmin -mabi=lp64d -O2" } */

/* Regression test for the IME geometry walker: peel_to_mmacc_def must
   look through __riscv_vget_v_*  / __riscv_vlmul_trunc_v_*  /
   __riscv_vlmul_ext_v_*  when walking backward from a tile store to
   its producing mmacc.

   The canonical idiom for extracting the partial-VL output of a
   widening mmacc is

       c_tile  = __riscv_vfwmmacc_vv_f32mN_f16m1 (...);
       reg0    = __riscv_vget_v_f32mN_f32m1 (c_tile, 0);
       __riscv_vmts_v_f32m1 (ptr, ldc, reg0, vl);

   Before the fix peel_to_mmacc_def stopped at the vget call, the
   SSA-dataflow layer of the geometry walker failed for the m1 vmts,
   and the walker fell through to the per-BB / function-global
   layers.  When two MUL_C variants were inlined into the same
   function (templates dispatched through a runtime switch), the
   function-global layer returned {0, 0} and the m1 vmts dropped to
   the standalone-MUL_C=1 probe -- wrong lambda, runtime trap or
   silent miscompare depending on VLEN.

   This test verifies that the vget pattern compiles cleanly under
   runtime-VLEN and that all IME consumers reach codegen.  The bug is
   primarily a runtime correctness issue (wrong vtype.lambda visible
   to the m1 vmts) and is hard to express precisely as an asm regex;
   behavioural correctness is exercised by
   examples/src/demo-streaming-partial-vl.cxx under QEMU at VLENs
   {256, 512, 1024} after stripping the `__attribute__((noinline))'
   workaround on the streaming_gemm_impl<MUL_C> templates.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

#pragma riscv intrinsic "vector"

extern unsigned char A[], B[], C4[], C8[];
extern int sink;

/* Two MUL_C variants in one function, each with the
   vfwmmacc -> vget_v_f32mN_f32m1 -> vmts_v_f32m1 chain.  The shape
   that drove the original failure: layer-1 SSA must follow the vget
   to see the producing mmacc; otherwise layer-3 sees both m4 and m8
   geometries in the same function and falls back to MUL_C=1.  */
void
walker_vget_peel_test (size_t vl_in, size_t vl_partial4,
		       size_t vl_partial8, size_t vl_m1)
{
  _Float16 *fA = (_Float16 *) A;
  _Float16 *fB = (_Float16 *) B;
  float    *f4 = (float *)    C4;
  float    *f8 = (float *)    C8;

  /* MUL_C = 4 chain.  */
  vfloat16m1_t a4 = __riscv_vmtl_v_f16m1 (fA, 4, vl_in);
  vfloat16m1_t b4 = __riscv_vmtl_v_f16m1 (fB, 4, vl_in);
  vfloat32m4_t z4 = __riscv_vreinterpret_v_u32m4_f32m4 (
		       __riscv_vmv_v_x_u32m4 (0, vl_partial4));
  vfloat32m4_t c4 = __riscv_vfwmmacc_vv_f32m4_f16m1 (z4, a4, b4, vl_partial4);
  vfloat32m1_t r4 = __riscv_vget_v_f32m4_f32m1 (c4, 0);
  __riscv_vmts_v_f32m1 (f4, 4, r4, vl_m1);

  /* MUL_C = 8 chain in the same function.  */
  vfloat16m1_t a8 = __riscv_vmtl_v_f16m1 (fA, 4, vl_in);
  vfloat16m1_t b8 = __riscv_vmtl_v_f16m1 (fB, 4, vl_in);
  vfloat32m8_t z8 = __riscv_vreinterpret_v_u32m8_f32m8 (
		       __riscv_vmv_v_x_u32m8 (0, vl_partial8));
  vfloat32m8_t c8 = __riscv_vfwmmacc_vv_f32m8_f16m1 (z8, a8, b8, vl_partial8);
  vfloat32m1_t r8 = __riscv_vget_v_f32m8_f32m1 (c8, 0);
  __riscv_vmts_v_f32m1 (f8, 4, r8, vl_m1);

  sink ^= (int) (long) (f4 + (long) f8);
}

unsigned char A[4096], B[4096], C4[4096], C8[4096];
int sink;

/* Both vget chains survive through to codegen, both vfwmmacc remain,
   and the four input loads + two m1 stores are emitted.  */
/* { dg-final { scan-assembler-times {\tvmtl\.v\t}      4 } } */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 2 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t}      2 } } */
