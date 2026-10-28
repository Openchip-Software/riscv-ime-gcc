/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma -mabi=lp64d -O2" } */

/* The storage width S of a tile load/store _as_e{S} form is at least the
   element width, so there are no _as_e{S} forms with S narrower than the
   element type.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint32m1_t
f1 (const void *p, long ld, size_t vl)
{
  return __riscv_vmtl_v_i32m1_as_e16 (p, ld, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}

void
f2 (void *p, long ld, vint32m1_t v, size_t vl)
{
  __riscv_vmts_v_i32m1_as_e16 (p, ld, v, vl); /* { dg-error "implicit declaration" } */
}

vfloat64m1_t
f3 (const void *p, long ld, size_t vl)
{
  return __riscv_vmttl_v_f64m1_as_e32 (p, ld, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}

vint16m1_t
f4 (const void *p, long ld, size_t vl)
{
  return __riscv_vmtl_v_i16m1_as_e8_L2 (p, ld, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}

/* S equal to or wider than the element width is still available.  */
vint16m1_t
f5 (const void *p, long ld, size_t vl)
{
  return __riscv_vmtl_v_i16m1_as_e32 (p, ld, vl);
}
