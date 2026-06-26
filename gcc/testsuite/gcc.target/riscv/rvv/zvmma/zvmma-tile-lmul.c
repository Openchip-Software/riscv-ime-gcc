/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* LMUL > 1 tile load/store variants.  Tile instructions use a
   notional MUL_C=1 for the lambda constraint, so the LMUL of the
   data vector register is independent of the lambda geometry --
   all whole LMULs (m1, m2, m4, m8) are supported.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

vint8m2_t
l_m2 (const signed char *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmtl_v_i8m2 (p, ld, vl);
}

vint8m4_t
l_m4 (const signed char *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmtl_v_i8m4 (p, ld, vl);
}

vint8m8_t
l_m8 (const signed char *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmtl_v_i8m8 (p, ld, vl);
}

void
s_m4 (int *p, ptrdiff_t ld, vint32m4_t v, size_t vl)
{
  __riscv_vmts_v_i32m4 (p, ld, v, vl);
}

void
ts_m2 (signed char *p, ptrdiff_t ld, vint8m2_t v, size_t vl)
{
  __riscv_vmtts_v_i8m2 (p, ld, v, vl);
}

/* { dg-final { scan-assembler-times {\tvmtl\.v\t} 3 } } */
/* { dg-final { scan-assembler-times {\tvmts\.v\t} 1 } } */
/* { dg-final { scan-assembler-times {\tvmtts\.v\t} 1 } } */
/* Each LMUL variant produces a distinct vtype constant, so exactly
   five vsetvl instructions are emitted.  */
/* { dg-final { scan-assembler-times {\tvsetvli\t} 5 } } */
