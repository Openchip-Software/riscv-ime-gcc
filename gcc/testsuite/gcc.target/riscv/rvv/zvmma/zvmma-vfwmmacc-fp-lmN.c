/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvfmm_zbb_zvfh_zvfbfmin -mabi=lp64d -O3" } */

/* Regression test for the FP/OFP8 widening `_lm{N}' intrinsics.

   Spec §2614-2618 + §2657: the `_lm{N}' qualifier applies uniformly
   to FP and integer widening mmaccs.  The vs1/vs2 argument types
   directly reflect LMUL as `vX{EEW}m{LMUL}_t'.

   Prior to this commit, the GCC implementation only enumerated
   LMUL=1 entries for FP widening (`ime_{fp16,bf16,fp32,...}_lmul1_vector'
   base types and matching RTL patterns / function defs), making
   `__riscv_vfwmmacc_vv_f32m{N}_f16m{2,4,8}_lm{2,4,8}' inaccessible.

   This test exercises the full LMUL ladder for FP16 -> FP32,
   BF16 -> FP32, and FP32 -> FP64 widening forms.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

/* FP16 -> FP32 input LMUL=2, 4, 8.  */
vfloat32m4_t f16_lm2 (vfloat32m4_t vd, vfloat16m2_t a, vfloat16m2_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f32m4_f16m2_lm2 (vd, a, b, vl); }

vfloat32m4_t f16_lm4 (vfloat32m4_t vd, vfloat16m4_t a, vfloat16m4_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f32m4_f16m4_lm4 (vd, a, b, vl); }

vfloat32m4_t f16_lm8 (vfloat32m4_t vd, vfloat16m8_t a, vfloat16m8_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f32m4_f16m8_lm8 (vd, a, b, vl); }

/* BF16 -> FP32 input LMUL=2, 4, 8.  */
vfloat32m4_t bf16_lm2 (vfloat32m4_t vd, vbfloat16m2_t a, vbfloat16m2_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f32m4_bf16m2_lm2 (vd, a, b, vl); }

vfloat32m4_t bf16_lm4 (vfloat32m4_t vd, vbfloat16m4_t a, vbfloat16m4_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f32m4_bf16m4_lm4 (vd, a, b, vl); }

vfloat32m4_t bf16_lm8 (vfloat32m4_t vd, vbfloat16m8_t a, vbfloat16m8_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f32m4_bf16m8_lm8 (vd, a, b, vl); }

/* FP32 -> FP64 input LMUL=2, 4, 8.  */
vfloat64m4_t f32_lm2 (vfloat64m4_t vd, vfloat32m2_t a, vfloat32m2_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f64m4_f32m2_lm2 (vd, a, b, vl); }

vfloat64m4_t f32_lm4 (vfloat64m4_t vd, vfloat32m4_t a, vfloat32m4_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f64m4_f32m4_lm4 (vd, a, b, vl); }

vfloat64m4_t f32_lm8 (vfloat64m4_t vd, vfloat32m8_t a, vfloat32m8_t b, size_t vl)
{ return __riscv_vfwmmacc_vv_f64m4_f32m8_lm8 (vd, a, b, vl); }

/* Each function should emit a single `vfwmmacc.vv' instruction.  */
/* { dg-final { scan-assembler-times {vfwmmacc\.vv} 9 } } */
