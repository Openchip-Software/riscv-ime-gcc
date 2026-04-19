/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvfbfmin_zbb -mabi=lp64d -O2" } */

/* Verify that vmtl.v / vmts.v accept FP element types (FP16, BF16,
   FP32, FP64) directly, without needing reinterpret casts.  Uses
   runtime lambda dispatch (no -mrvv-vector-bits) so every SEW is
   valid regardless of target VLEN.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

vfloat16m1_t
load_f16  (const _Float16 *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_f16m1 (p, ld, vl); }

void
store_f16 (_Float16 *p, ptrdiff_t ld, vfloat16m1_t v, size_t vl)
{ __riscv_vmts_v_f16m1 (p, ld, v, vl); }

vbfloat16m1_t
load_bf16 (const __bf16 *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_bf16m1 (p, ld, vl); }

void
store_bf16 (__bf16 *p, ptrdiff_t ld, vbfloat16m1_t v, size_t vl)
{ __riscv_vmts_v_bf16m1 (p, ld, v, vl); }

vfloat32m1_t
load_f32  (const float *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_f32m1 (p, ld, vl); }

void
store_f32 (float *p, ptrdiff_t ld, vfloat32m1_t v, size_t vl)
{ __riscv_vmts_v_f32m1 (p, ld, v, vl); }

vfloat64m1_t
load_f64  (const double *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_f64m1 (p, ld, vl); }

void
store_f64 (double *p, ptrdiff_t ld, vfloat64m1_t v, size_t vl)
{ __riscv_vmts_v_f64m1 (p, ld, v, vl); }

/* Four loads + four stores.  */
/* { dg-final { scan-assembler-times {\tvmtl\.v\t} 4 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t} 4 } } */
