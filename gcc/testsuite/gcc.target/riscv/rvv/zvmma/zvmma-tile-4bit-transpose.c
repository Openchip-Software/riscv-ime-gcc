/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma -mabi=lp64d -O2" } */

/* The transposing tile loads and stores cannot transpose sub-byte
   elements, so they do not exist for the 4-bit types.  */

typedef __SIZE_TYPE__ size_t;
typedef __UINT8_TYPE__ uint8_t;
#pragma riscv intrinsic "vector"

vint4m1_t
ld (const uint8_t *p, size_t ld, size_t vl)
{
  return __riscv_vmttl_v_i4m1 (p, ld, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}

void
st (uint8_t *p, size_t ld, vfloat4e2m1m1_t v, size_t vl)
{
  __riscv_vmtts_v_f4e2m1m1 (p, ld, v, vl); /* { dg-error "implicit declaration" } */
}
