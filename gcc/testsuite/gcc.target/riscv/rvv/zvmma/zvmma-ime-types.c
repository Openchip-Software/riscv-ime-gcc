/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma -mabi=lp64d -O2" } */

/* Verify that IME packed-in-bytes vector types are registered and
   usable as function parameters and local variables.  */

#pragma riscv intrinsic "vector"

/* Types must be usable as function parameters.  */
void use_i4 (vint4m1_t x) { (void) x; }
void use_u4 (vuint4m1_t x) { (void) x; }
void use_f8e4m3 (vfloat8e4m3m1_t x) { (void) x; }
void use_f8e5m2 (vfloat8e5m2m1_t x) { (void) x; }
void use_f4e2m1 (vfloat4e2m1m1_t x) { (void) x; }
