/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* MVP zvvmttls transposing tile load (vmttl.v): LMUL=1 integer
   variants, dynamic lambda, unmasked.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

vint8m1_t
tload_i8 (const signed char *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmttl_v_i8m1 (p, ld, vl);
}

vuint8m1_t
tload_u8 (const unsigned char *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmttl_v_u8m1 (p, ld, vl);
}

vint32m1_t
tload_i32 (const int *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmttl_v_i32m1 (p, ld, vl);
}

/* { dg-final { scan-assembler-times {\tvmttl\.v\t} 3 } } */
