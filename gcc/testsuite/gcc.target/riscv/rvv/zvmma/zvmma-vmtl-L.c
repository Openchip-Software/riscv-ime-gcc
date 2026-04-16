/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* vmtl.v with immediate "Lλ" override.  Each _L{N} variant emits a
   ",L{N}" suffix on the vmtl.v instruction, overriding vtype.lambda
   for the duration of that instruction only.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

vint8m1_t L1  (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_i8m1_L1  (p, ld, vl); }
vint8m1_t L2  (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_i8m1_L2  (p, ld, vl); }
vint8m1_t L4  (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_i8m1_L4  (p, ld, vl); }
vint8m1_t L8  (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_i8m1_L8  (p, ld, vl); }
vint8m1_t L16 (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_i8m1_L16 (p, ld, vl); }
vint8m1_t L32 (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_i8m1_L32 (p, ld, vl); }
vint8m1_t L64 (const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_i8m1_L64 (p, ld, vl); }

/* Masked + Lλ composition.  */
vint8m1_t Lm  (vbool8_t m, const signed char *p, ptrdiff_t ld, size_t vl)
{ return __riscv_vmtl_v_i8m1_L4_m (m, p, ld, vl); }

/* Each L{N} token appears exactly once in the assembly.  */
/* { dg-final { scan-assembler-times {,L1\n}  1 } } */
/* { dg-final { scan-assembler-times {,L2\n}  1 } } */
/* { dg-final { scan-assembler-times {,L4\n}  1 } } */
/* { dg-final { scan-assembler-times {,L8\n}  1 } } */
/* { dg-final { scan-assembler-times {,L16\n} 1 } } */
/* { dg-final { scan-assembler-times {,L32\n} 1 } } */
/* { dg-final { scan-assembler-times {,L64\n} 1 } } */
/* The masked L4 variant also carries ",v0.t".  */
/* { dg-final { scan-assembler-times {,L4,v0\.t} 1 } } */
