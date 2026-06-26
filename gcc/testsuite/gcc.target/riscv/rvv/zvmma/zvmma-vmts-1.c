/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* MVP zvvmtls tile store (vmts.v): LMUL=1 integer variants, dynamic
   lambda from vtype, unmasked.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

void
store_i8 (signed char *p, ptrdiff_t ld, vint8m1_t v, size_t vl)
{
  __riscv_vmts_v_i8m1 (p, ld, v, vl);
}

void
store_u8 (unsigned char *p, ptrdiff_t ld, vuint8m1_t v, size_t vl)
{
  __riscv_vmts_v_u8m1 (p, ld, v, vl);
}

void
store_i32 (int *p, ptrdiff_t ld, vint32m1_t v, size_t vl)
{
  __riscv_vmts_v_i32m1 (p, ld, v, vl);
}

void
store_u32 (unsigned int *p, ptrdiff_t ld, vuint32m1_t v, size_t vl)
{
  __riscv_vmts_v_u32m1 (p, ld, v, vl);
}

/* { dg-final { scan-assembler-times {\tvmts\.v\t} 4 } } */
/* { dg-final { scan-assembler-times {\tvsetvli\t} 4 } } */
