/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* MVP zvvmttls transposing tile store (vmtts.v): LMUL=1 integer
   variants, dynamic lambda, unmasked.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

void
tstore_i8 (signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{
  __riscv_vmtts_v_i8m1 (p, ld, v, vl);
}

void
tstore_u8 (unsigned char *p, ptrdiff_t ld, vuint8m1_t v, size_t vl)
{
  __riscv_vmtts_v_u8m1 (p, ld, v, vl);
}

void
tstore_i32 (int *p, ptrdiff_t ld, vint32m1_t v, size_t vl)
{
  __riscv_vmtts_v_i32m1 (p, ld, v, vl);
}

/* { dg-final { scan-assembler-times {\tvmtts\.v\t} 3 } } */
