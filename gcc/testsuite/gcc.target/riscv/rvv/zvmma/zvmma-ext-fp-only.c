/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvvfmm -mabi=lp64d -O2" } */

/* The floating-point m16 types and the lambda intrinsics are available
   without the integer extensions.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vfloat32m8_t
f (vfloat32m8_t lo, vfloat32m8_t hi, vfloat32m1_t a, vfloat32m1_t b,
   size_t vl)
{
  vfloat32m16_t acc = __riscv_ime_vpair_f32m16 (lo, hi);
  acc = __riscv_vfmmacc_vv_f32m16 (acc, a, b, vl);
  return __riscv_ime_vunpairhi_f32m16 (acc);
}

size_t g (void) { return __riscv_vsetlambda (2); }
size_t h (void) { return __riscv_ime_lambda (); }

/* { dg-final { scan-assembler {\mvfmmacc\.vv\M} } } */
