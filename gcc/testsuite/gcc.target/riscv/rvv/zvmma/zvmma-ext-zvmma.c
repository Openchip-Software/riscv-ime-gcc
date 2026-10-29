/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma -mabi=lp64d -O2" } */

/* Zvmma implies every Zvvm extension, the microscaling ones included.  */

#if !defined (__riscv_zvvmm) || !defined (__riscv_zvvfmm) \
    || !defined (__riscv_zvvmtls) || !defined (__riscv_zvvmttls) \
    || !defined (__riscv_zvvi4i8mm) || !defined (__riscv_zvvfp64mm) \
    || !defined (__riscv_zvvxofp4ofp8mm) || !defined (__riscv_zvvxnofp8fp64mm) \
    || !defined (__riscv_zvvxi4fp16mm) || !defined (__riscv_zvvxni8fp64mm)
#error "zvmma does not imply every Zvvm extension"
#endif

int dummy;
