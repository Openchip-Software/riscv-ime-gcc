/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Immediate-lambda variants for vmts.v, vmttl.v and vmtts.v.  The
   vmtl.v case is covered by zvmma-vmtl-L.c; this test exercises the
   remaining three instructions plus the masked composition.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

void s_L1  (signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{ __riscv_vmts_v_i8m1_L1  (p, ld, v, vl); }
void s_L8  (signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{ __riscv_vmts_v_i8m1_L8  (p, ld, v, vl); }
void s_L64 (signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{ __riscv_vmts_v_i8m1_L64 (p, ld, v, vl); }

vint8m1_t tl_L2  (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmttl_v_i8m1_L2  (p, ld, vl); }
vint8m1_t tl_L16 (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmttl_v_i8m1_L16 (p, ld, vl); }

void ts_L4  (signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{ __riscv_vmtts_v_i8m1_L4  (p, ld, v, vl); }
void ts_L32 (signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{ __riscv_vmtts_v_i8m1_L32 (p, ld, v, vl); }

/* Masked + Lλ composition on a store.  */
void sm_L4  (vbool8_t m, signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{ __riscv_vmts_v_i8m1_L4_m (m, p, ld, v, vl); }

/* Store instruction + L token combinations.  */
/* { dg-final { scan-assembler-times {\tvmts\.v\t[^\n]*,L1\n}  1 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t[^\n]*,L8\n}  1 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t[^\n]*,L64\n} 1 } } */
/* { dg-final { scan-assembler-times {\tvmttl\.v\t[^\n]*,L2\n}  1 } } */
/* { dg-final { scan-assembler-times {\tvmttl\.v\t[^\n]*,L16\n} 1 } } */
/* { dg-final { scan-assembler-times {\tvmtts\.v\t[^\n]*,L4\n}  1 } } */
/* { dg-final { scan-assembler-times {\tvmtts\.v\t[^\n]*,L32\n} 1 } } */
/* Masked store with L4 carries both the L4 immediate and the
   ",v0.t" mask suffix.  */
/* { dg-final { scan-assembler-times {\tvmts\.v\t[^\n]*,L4,v0\.t} 1 } } */
