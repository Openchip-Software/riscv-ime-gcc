/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma -mabi=lp64d -O2" } */

/* IME accumulators are never fractional (EMUL_C is 1, 2, 4, 8 or 16), so
   there are no multiply-accumulate intrinsics with an mf2/mf4/mf8
   accumulator.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint8mf2_t
f1 (vint8mf2_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i8mf2 (vd, a, b, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}

vint16mf2_t
f2 (vint16mf2_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vwmmacc_vv_i16mf2 (vd, a, b, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}

vint32mf2_t
f3 (vint32mf2_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv_i32mf2 (vd, a, b, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}
