/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma -mabi=lp64d -O2" } */

/* The order-preserving tile loads and stores of the 4-bit types use
   SEW=8 storage and a uint8_t pointer, or SEW=S for _as_e{S}.  */

typedef __SIZE_TYPE__ size_t;
typedef __UINT8_TYPE__ uint8_t;
#pragma riscv intrinsic "vector"

vint4m1_t
ld_i4 (const uint8_t *p, size_t ld, size_t vl)
{
  return __riscv_vmtl_v_i4m1 (p, ld, vl);
}

vuint4m4_t
ld_u4_as_e32 (const void *p, size_t ld, size_t vl)
{
  return __riscv_vmtl_v_u4m4_as_e32 (p, ld, vl);
}

void
st_f4 (uint8_t *p, size_t ld, vfloat4e2m1m2_t v, size_t vl)
{
  __riscv_vmts_v_f4e2m1m2_L4 (p, ld, v, vl);
}

void
st_i4_m (vbool8_t m, uint8_t *p, size_t ld, vint4m1_t v, size_t vl)
{
  __riscv_vmts_v_i4m1_m (m, p, ld, v, vl);
}

/* { dg-final { scan-assembler-times {\tvsetvli\tzero,a[0-9],e8,m1,ta,ma} 2 } } */
/* { dg-final { scan-assembler {\tvsetvli\tzero,a[0-9],e32,m4,ta,ma} } } */
/* { dg-final { scan-assembler {\tvmts\.v\tv[0-9]+,0\(a0\),a1,L4\n} } } */
/* { dg-final { scan-assembler {\tvmts\.v\tv[0-9]+,0\(a0\),a1,v0\.t\n} } } */
