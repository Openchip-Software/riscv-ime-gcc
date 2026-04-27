/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Static-VLEN counterpart to zvmma-widening-tile-probe.c.  At zvl512b with
   the W=2 widening f32 <- f16 mmacc + paired f16 vmtl loads:

      vmtl  (ISEW=16, inherits MUL_C = W * MUL_C_acc = 2)
        --> divisor 512/(16*2) = 16, lambda_enc=3 (lambda=4)

      vfwmmacc  (ASEW=32, MUL_C=1)
        --> divisor 512/(32*1) = 16, lambda_enc=3 (lambda=4)

   Both vsetvl constants must therefore carry the same lambda field.
   Without the static-path inheritance, the vmtl falls back to MUL_C=1
   which gives divisor 512/16 = 32 -- log2 odd -> sorry().  */

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

/* The two IME vsetvl constants encode SEW=16 (low byte 0xC8 = 200) and
   SEW=32 (low byte 0xD0 = 208), but with the same lambda field --
   3458764513820541128 = 0x30000000000000C8 and
   3458764513820541136 = 0x30000000000000D0.  Asserting on these full
   constants verifies both that the inheritance is applied (vmtl gets a
   non-trivial lambda) and that the two SEW vtypes share a lambda
   field.  */
/* { dg-final { scan-assembler "li\[ \t\]+\[a-z\]\[a-z0-9\]+,3458764513820541128\n" } } */
/* { dg-final { scan-assembler "li\[ \t\]+\[a-z\]\[a-z0-9\]+,3458764513820541136\n" } } */
/* { dg-final { scan-assembler-times {\tvmtl\.v\t}      2 } } */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t}      1 } } */
