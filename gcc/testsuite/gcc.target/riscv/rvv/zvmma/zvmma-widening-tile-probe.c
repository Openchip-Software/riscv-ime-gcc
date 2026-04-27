/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb_zvfh -mabi=lp64d -O2" } */

/* Verify that for widening FP mmacc kernels the input-side vmtl probe
   uses the same lambda divisor as the surrounding mmacc.  Per spec,
   both share vtype.lambda; if the vmtl probe used divisor VLEN/ISEW
   (m1) and the mmacc used VLEN/(ASEW*MUL_C_acc), the lambdas would
   differ by a factor of W (the widening factor) and the data layout
   would not match.

   The geometry walker should detect that the surrounding f32m1
   mmacc has a wider SEW than the f16m1 vmtl, infer W=2, and use
   MUL_C_tile = W * MUL_C_acc = 2 for the vmtl's lambda derivation.
   Both probes then divide by 32 (= ISEW*MUL_C_tile = ASEW*MUL_C_acc).  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

void w2_kernel (const _Float16 *A, const _Float16 *B, float *C, size_t vl,
                ptrdiff_t lda, ptrdiff_t ldb, ptrdiff_t ldc)
{
  vfloat16m1_t va = __riscv_vmtl_v_f16m1 (A, lda, vl);
  vfloat16m1_t vb = __riscv_vmtl_v_f16m1 (B, ldb, vl);
  vfloat32m1_t vacc = __riscv_vfmv_v_f_f32m1 (0.0f, vl);
  vacc = __riscv_vfwmmacc_vv_f32m1_f16m1 (vacc, va, vb, vl);
  __riscv_vmts_v_f32m1 (C, ldc, vacc, vl);
}

/* The vmtl probe must divide vlen by ISEW*W*MUL_C_acc = 16*2*1 = 32,
   which appears as `srli ..., ..., 5' on the runtime-VLEN path.
   Without the gap-7 fix the divisor would be 16 (`srli ..., ..., 4').  */
/* { dg-final { scan-assembler "srli\[ \t\]+\[a-z\]\[a-z0-9\]+,\[a-z\]\[a-z0-9\]+,5\n" } } */
/* The vmtl and vfwmmacc must run with the same vtype.lambda value.
   The high bits (lambda field) come from a single `slli ..., 60' that
   feeds both setups.  */
/* { dg-final { scan-assembler-times {\tslli\t[a-z][a-z0-9]+,[a-z][a-z0-9]+,60\n} 1 } } */
/* { dg-final { scan-assembler-times {\tvmtl\.v\t}    2 } } */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t}    1 } } */
