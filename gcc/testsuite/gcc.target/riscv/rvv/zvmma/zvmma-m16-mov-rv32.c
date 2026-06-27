/* { dg-do compile } */
/* { dg-options "-march=rv32gcv_zvmma -mabi=ilp32d -O2" } */

/* The m16 mov splitter walks the upper-half memory address through a
   Pmode scratch (`csrr vlenb' shifted by log2 (LMUL_HALF), then added
   to the lower-half base).  Before commit
   `riscv: switch m16 register allocation to Design A (drop VM16_REGS)'
   set the scratch type to `Pmode' via the `P' iterator the scratch
   was hard-wired to DImode -- correct for LP64 but a mode mismatch
   on ILP32 that ICEd at split2 ("unrecognizable insn:
       (ashift:SI (reg:DI ...) (const_int 3))").

   This test pins the RV32 path: store an m16 through a pointer, which
   forces the splitter to take the mem<->reg branch and emit the
   upper-half address.  No ICE; address arithmetic stays in SImode
   (`slli ..., 3' + `add' on RV32, not `slliw'/`addw' tricks).  */

#pragma riscv intrinsic "vector"

extern vint32m16_t *gp;

void
f (vint32m8_t lo, vint32m8_t hi)
{
  *gp = __riscv_ime_vpair_i32m16 (lo, hi);
}

/* { dg-final { scan-assembler "csrr" } } */
/* { dg-final { scan-assembler "slli" } } */
/* { dg-final { scan-assembler-not "slliw" } } */
