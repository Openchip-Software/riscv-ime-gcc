/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl2048b -mabi=lp64d -O2" } */

/* Companion to `zvmma-min-vlen-reject.c': VLEN=2048 with Zvvmm is
   the largest setting that keeps VLEN * LMUL (= 2048 * 16 = 32768)
   within the `poly_uint16' bound, so the override-options sorry must
   NOT fire here.  m16-aware codegen must succeed for a trivial
   translation unit.  */

#pragma riscv intrinsic "vector"

int x;
