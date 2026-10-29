/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvvmtls -mabi=lp64d -O2" } */

/* The lambda intrinsics are available with the tile load/store extension
   alone; the multiply-accumulates are not.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

size_t f (void) { return __riscv_vsetlambda (2); }
size_t g (void) { return __riscv_ime_lambda (); }
size_t h (void) { return __riscv_ime_vlen (); }

vint32m1_t
i8i32 (vint32m1_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv_i32m1 (vd, a, b, vl); /* { dg-error "requires the 'zvvi8i32mm' ISA extension" } */
}
