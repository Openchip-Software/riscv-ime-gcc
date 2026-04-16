/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Masked (_m) variants of the tile load/store intrinsics.  The
   unmasked and masked forms share the same RTL pattern -- operand
   1 accepts either a real mask register or an all-ones synthetic
   mask -- and the pattern's %p1 modifier prints ",v0.t" only for
   the real-mask case.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

vint8m1_t
load_m (vbool8_t m, const signed char *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmtl_v_i8m1_m (m, p, ld, vl);
}

void
store_m (vbool8_t m, signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{
  __riscv_vmts_v_i8m1_m (m, p, ld, v, vl);
}

vint32m1_t
tload_m (vbool32_t m, const int *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmttl_v_i32m1_m (m, p, ld, vl);
}

void
tstore_m (vbool32_t m, int *p, ptrdiff_t ld, vint32m1_t v, size_t vl)
{
  __riscv_vmtts_v_i32m1_m (m, p, ld, v, vl);
}

/* Each masked variant emits its mnemonic exactly once and includes
   the ",v0.t" mask suffix.  */
/* { dg-final { scan-assembler-times {\tvmtl\.v\t[^\n]*,v0\.t} 1 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t[^\n]*,v0\.t} 1 } } */
/* { dg-final { scan-assembler-times {\tvmttl\.v\t[^\n]*,v0\.t} 1 } } */
/* { dg-final { scan-assembler-times {\tvmtts\.v\t[^\n]*,v0\.t} 1 } } */
