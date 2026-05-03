/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb -mabi=lp64d -O2" } */

/* Gap #14 commit 2: typed OFP8 immediate-lambda tile load/store.

   The _L<N> family overrides the dynamic vtype.lambda for the
   duration of one instruction; commit 1 ('typed OFP8 tile load/store
   intrinsics') already wired the OFP8 vector types into the shared
   type set fiu_lmul_ge1_ops, so the imm-lambda surfaces

     __riscv_vmtl_v_f8e<X>m<N>_L<L>
     __riscv_vmts_v_f8e<X>m<N>_L<L>
     __riscv_vmttl_v_f8e<X>m<N>_L<L>
     __riscv_vmtts_v_f8e<X>m<N>_L<L>

   come along for free.  This testcase pins that down by exercising
   each of the four mnemonics with a distinct lambda override and
   verifying the assembler-level Lλ suffix is emitted.  */

typedef __SIZE_TYPE__ size_t;
typedef unsigned char uint8_t;

#pragma riscv intrinsic "vector"

vfloat8e4m3m1_t l_e4m3_L4 (const uint8_t *p, size_t vl)
{ return __riscv_vmtl_v_f8e4m3m1_L4 (p, 4, vl); }

vfloat8e5m2m4_t l_e5m2_L8 (const uint8_t *p, size_t vl)
{ return __riscv_vmtl_v_f8e5m2m4_L8 (p, 4, vl); }

void s_e4m3_L2 (uint8_t *p, vfloat8e4m3m1_t v, size_t vl)
{ __riscv_vmts_v_f8e4m3m1_L2 (p, 4, v, vl); }

vfloat8e4m3m1_t lt_e4m3_L16 (const uint8_t *p, size_t vl)
{ return __riscv_vmttl_v_f8e4m3m1_L16 (p, 4, vl); }

void st_e5m2_L32 (uint8_t *p, vfloat8e5m2m1_t v, size_t vl)
{ __riscv_vmtts_v_f8e5m2m1_L32 (p, 4, v, vl); }

/* { dg-final { scan-assembler-times {\tvmtl\.v\t[^,]+,[^,]+,[^,]+,L4\n}  1 } } */
/* { dg-final { scan-assembler-times {\tvmtl\.v\t[^,]+,[^,]+,[^,]+,L8\n}  1 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t[^,]+,[^,]+,[^,]+,L2\n}  1 } } */
/* { dg-final { scan-assembler-times {\tvmttl\.v\t[^,]+,[^,]+,[^,]+,L16\n} 1 } } */
/* { dg-final { scan-assembler-times {\tvmtts\.v\t[^,]+,[^,]+,[^,]+,L32\n} 1 } } */
