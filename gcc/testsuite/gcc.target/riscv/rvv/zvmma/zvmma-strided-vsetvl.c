/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma -mabi=lp64d -O2" } */

/* Ordinary strided loads and stores are not IME tile loads and stores:
   with IME enabled they still demand only the SEW/LMUL ratio, so one
   vsetvli serves all four.  */

#include <stddef.h>
#pragma riscv intrinsic "vector"

void
f (int *a, signed char *b, ptrdiff_t s, size_t vl)
{
  vint32m4_t x = __riscv_vlse32_v_i32m4 (a, s, vl);
  vint8m1_t y = __riscv_vlse8_v_i8m1 (b, s, vl);
  __riscv_vsse32_v_i32m4 (a, s, x, vl);
  __riscv_vsse8_v_i8m1 (b, s, y, vl);
}

/* { dg-final { scan-assembler-times {\mvsetvli\M} 1 } } */
