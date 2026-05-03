/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb -mabi=lp64d -O2" } */

/* Gap #14 commit 1: typed OFP8 tile load/store intrinsics.

   Adding the OFP8 vector types to the existing tile load/store type
   set (fiu_lmul_ge1_ops) gives the user-facing surface

     __riscv_vmtl_v_f8e<X>m<N> / __riscv_vmts_v_f8e<X>m<N>
     __riscv_vmttl_v_f8e<X>m<N> / __riscv_vmtts_v_f8e<X>m<N>

   for X in {4m3, 5m2} and N in {1, 2, 4, 8}.  The MD pattern
   (@pred_vmtl<mode> et al., VI_LMUL1 mode iterator) handles RVVM*QI
   unchanged; the dispatch is unambiguous via the canonical-type-keyed
   overloaded_hash (commit 41568e53a15e).  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef unsigned char uint8_t;

#pragma riscv intrinsic "vector"

vfloat8e4m3m1_t l_e4m3_m1 (const uint8_t *p, size_t vl)
{ return __riscv_vmtl_v_f8e4m3m1 (p, 4, vl); }

vfloat8e4m3m4_t l_e4m3_m4 (const uint8_t *p, size_t vl)
{ return __riscv_vmtl_v_f8e4m3m4 (p, 4, vl); }

vfloat8e5m2m1_t l_e5m2_m1 (const uint8_t *p, size_t vl)
{ return __riscv_vmtl_v_f8e5m2m1 (p, 4, vl); }

vfloat8e5m2m4_t l_e5m2_m4 (const uint8_t *p, size_t vl)
{ return __riscv_vmtl_v_f8e5m2m4 (p, 4, vl); }

void s_e4m3_m1 (uint8_t *p, vfloat8e4m3m1_t v, size_t vl)
{ __riscv_vmts_v_f8e4m3m1 (p, 4, v, vl); }

void s_e5m2_m4 (uint8_t *p, vfloat8e5m2m4_t v, size_t vl)
{ __riscv_vmts_v_f8e5m2m4 (p, 4, v, vl); }

vfloat8e4m3m1_t lt_e4m3_m1 (const uint8_t *p, size_t vl)
{ return __riscv_vmttl_v_f8e4m3m1 (p, 4, vl); }

void st_e5m2_m1 (uint8_t *p, vfloat8e5m2m1_t v, size_t vl)
{ __riscv_vmtts_v_f8e5m2m1 (p, 4, v, vl); }

/* { dg-final { scan-assembler-times {\tvmtl\.v} 4 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v} 2 } } */
/* { dg-final { scan-assembler-times {\tvmttl\.v} 1 } } */
/* { dg-final { scan-assembler-times {\tvmtts\.v} 1 } } */
