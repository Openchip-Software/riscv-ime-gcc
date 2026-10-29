/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvvmm_zvvxnofp8fp32mm -mabi=lp64d -O2" } */

/* Zvvmm implies every individual integer multiply-accumulate extension
   and no floating-point one.  A microscaling extension with block size
   16 implies the one with block size 32, and that its base extension.  */

#if !defined (__riscv_zvvmm) || !defined (__riscv_zvvi4i8mm) \
    || !defined (__riscv_zvvi8i32mm) || !defined (__riscv_zvvi16mm) \
    || !defined (__riscv_zvvi64mm)
#error "zvvmm does not imply the integer extensions"
#endif

#if !defined (__riscv_zvvxofp8fp32mm) || !defined (__riscv_zvvofp8fp32mm)
#error "zvvxnofp8fp32mm does not imply zvvxofp8fp32mm and zvvofp8fp32mm"
#endif

#if defined (__riscv_zvvfmm) || defined (__riscv_zvvfp32mm) \
    || defined (__riscv_zvvxofp8fp64mm)
#error "unexpected floating-point extension"
#endif

int dummy;

/* { dg-final { scan-assembler {\.attribute arch, "[^"]*_zvvi8i32mm1p0_[^"]*_zvvmm1p0_[^"]*_zvvxnofp8fp32mm1p0_zvvxofp8fp32mm1p0"} } } */
/* { dg-final { scan-assembler-not {zvvfmm} } } */
