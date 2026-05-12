/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm -mabi=lp64d -O2" } */

/* `__riscv_vsetlambda' accepts 0 or a positive power of two in
   {1, 2, 4, 8, 16, 32, 64}.  Anything else as a compile-time
   constant errors at expand time.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

size_t bad3   (void) { return __riscv_vsetlambda (3);   } /* { dg-error "must be 0 or a positive power of two" } */
size_t bad128 (void) { return __riscv_vsetlambda (128); } /* { dg-error "must be 0 or a positive power of two" } */
