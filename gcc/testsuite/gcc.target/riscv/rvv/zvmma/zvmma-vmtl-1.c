/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* MVP zvvmtls tile load (vmtl.v): LMUL=1 integer variants, dynamic
   lambda from vtype (no immediate Lλ), unmasked.  At zvl512b the
   valid (SEW, MUL_C=1) combinations are SEW=8 (lambda=8) and SEW=32
   (lambda=4); SEW=16 and SEW=64 at m1 have no integer-square-root
   lambda and are tested elsewhere.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

vint8m1_t
load_i8 (const signed char *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmtl_v_i8m1 (p, ld, vl);
}

vuint8m1_t
load_u8 (const unsigned char *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmtl_v_u8m1 (p, ld, vl);
}

vint32m1_t
load_i32 (const int *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmtl_v_i32m1 (p, ld, vl);
}

vuint32m1_t
load_u32 (const unsigned int *p, ptrdiff_t ld, size_t vl)
{
  return __riscv_vmtl_v_u32m1 (p, ld, vl);
}

/* { dg-final { scan-assembler-times {\tvmtl\.v\t} 4 } } */
/* { dg-final { scan-assembler-times {\tvsetvli\t} 4 } } */
