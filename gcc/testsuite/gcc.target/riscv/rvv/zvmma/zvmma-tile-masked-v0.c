/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* A masked tile load may not write v0.  With v1-v31 live and the mask
   dying at the load, v0 is the only free register, so the destination
   must be another register and something must be spilled.  */

typedef __SIZE_TYPE__ size_t;
typedef long ptrdiff_t;
#pragma riscv intrinsic "vector"
void
vmtl (signed char *p, signed char *q, ptrdiff_t ld, size_t vl, vbool8_t m)
{
  vint8m1_t x1 = __riscv_vle8_v_i8m1 (q + 1*64, vl);
  vint8m1_t x2 = __riscv_vle8_v_i8m1 (q + 2*64, vl);
  vint8m1_t x3 = __riscv_vle8_v_i8m1 (q + 3*64, vl);
  vint8m1_t x4 = __riscv_vle8_v_i8m1 (q + 4*64, vl);
  vint8m1_t x5 = __riscv_vle8_v_i8m1 (q + 5*64, vl);
  vint8m1_t x6 = __riscv_vle8_v_i8m1 (q + 6*64, vl);
  vint8m1_t x7 = __riscv_vle8_v_i8m1 (q + 7*64, vl);
  vint8m1_t x8 = __riscv_vle8_v_i8m1 (q + 8*64, vl);
  vint8m1_t x9 = __riscv_vle8_v_i8m1 (q + 9*64, vl);
  vint8m1_t x10 = __riscv_vle8_v_i8m1 (q + 10*64, vl);
  vint8m1_t x11 = __riscv_vle8_v_i8m1 (q + 11*64, vl);
  vint8m1_t x12 = __riscv_vle8_v_i8m1 (q + 12*64, vl);
  vint8m1_t x13 = __riscv_vle8_v_i8m1 (q + 13*64, vl);
  vint8m1_t x14 = __riscv_vle8_v_i8m1 (q + 14*64, vl);
  vint8m1_t x15 = __riscv_vle8_v_i8m1 (q + 15*64, vl);
  vint8m1_t x16 = __riscv_vle8_v_i8m1 (q + 16*64, vl);
  vint8m1_t x17 = __riscv_vle8_v_i8m1 (q + 17*64, vl);
  vint8m1_t x18 = __riscv_vle8_v_i8m1 (q + 18*64, vl);
  vint8m1_t x19 = __riscv_vle8_v_i8m1 (q + 19*64, vl);
  vint8m1_t x20 = __riscv_vle8_v_i8m1 (q + 20*64, vl);
  vint8m1_t x21 = __riscv_vle8_v_i8m1 (q + 21*64, vl);
  vint8m1_t x22 = __riscv_vle8_v_i8m1 (q + 22*64, vl);
  vint8m1_t x23 = __riscv_vle8_v_i8m1 (q + 23*64, vl);
  vint8m1_t x24 = __riscv_vle8_v_i8m1 (q + 24*64, vl);
  vint8m1_t x25 = __riscv_vle8_v_i8m1 (q + 25*64, vl);
  vint8m1_t x26 = __riscv_vle8_v_i8m1 (q + 26*64, vl);
  vint8m1_t x27 = __riscv_vle8_v_i8m1 (q + 27*64, vl);
  vint8m1_t x28 = __riscv_vle8_v_i8m1 (q + 28*64, vl);
  vint8m1_t x29 = __riscv_vle8_v_i8m1 (q + 29*64, vl);
  vint8m1_t x30 = __riscv_vle8_v_i8m1 (q + 30*64, vl);
  vint8m1_t x31 = __riscv_vle8_v_i8m1 (q + 31*64, vl);
  vint8m1_t t = __riscv_vmtl_v_i8m1_m (m, p, ld, vl);
  __riscv_vse8_v_i8m1 (q + 1*64, x1, vl);
  __riscv_vse8_v_i8m1 (q + 2*64, x2, vl);
  __riscv_vse8_v_i8m1 (q + 3*64, x3, vl);
  __riscv_vse8_v_i8m1 (q + 4*64, x4, vl);
  __riscv_vse8_v_i8m1 (q + 5*64, x5, vl);
  __riscv_vse8_v_i8m1 (q + 6*64, x6, vl);
  __riscv_vse8_v_i8m1 (q + 7*64, x7, vl);
  __riscv_vse8_v_i8m1 (q + 8*64, x8, vl);
  __riscv_vse8_v_i8m1 (q + 9*64, x9, vl);
  __riscv_vse8_v_i8m1 (q + 10*64, x10, vl);
  __riscv_vse8_v_i8m1 (q + 11*64, x11, vl);
  __riscv_vse8_v_i8m1 (q + 12*64, x12, vl);
  __riscv_vse8_v_i8m1 (q + 13*64, x13, vl);
  __riscv_vse8_v_i8m1 (q + 14*64, x14, vl);
  __riscv_vse8_v_i8m1 (q + 15*64, x15, vl);
  __riscv_vse8_v_i8m1 (q + 16*64, x16, vl);
  __riscv_vse8_v_i8m1 (q + 17*64, x17, vl);
  __riscv_vse8_v_i8m1 (q + 18*64, x18, vl);
  __riscv_vse8_v_i8m1 (q + 19*64, x19, vl);
  __riscv_vse8_v_i8m1 (q + 20*64, x20, vl);
  __riscv_vse8_v_i8m1 (q + 21*64, x21, vl);
  __riscv_vse8_v_i8m1 (q + 22*64, x22, vl);
  __riscv_vse8_v_i8m1 (q + 23*64, x23, vl);
  __riscv_vse8_v_i8m1 (q + 24*64, x24, vl);
  __riscv_vse8_v_i8m1 (q + 25*64, x25, vl);
  __riscv_vse8_v_i8m1 (q + 26*64, x26, vl);
  __riscv_vse8_v_i8m1 (q + 27*64, x27, vl);
  __riscv_vse8_v_i8m1 (q + 28*64, x28, vl);
  __riscv_vse8_v_i8m1 (q + 29*64, x29, vl);
  __riscv_vse8_v_i8m1 (q + 30*64, x30, vl);
  __riscv_vse8_v_i8m1 (q + 31*64, x31, vl);
  __riscv_vse8_v_i8m1 (q, t, vl);
}

void
vmttl (signed char *p, signed char *q, ptrdiff_t ld, size_t vl, vbool8_t m)
{
  vint8m1_t x1 = __riscv_vle8_v_i8m1 (q + 1*64, vl);
  vint8m1_t x2 = __riscv_vle8_v_i8m1 (q + 2*64, vl);
  vint8m1_t x3 = __riscv_vle8_v_i8m1 (q + 3*64, vl);
  vint8m1_t x4 = __riscv_vle8_v_i8m1 (q + 4*64, vl);
  vint8m1_t x5 = __riscv_vle8_v_i8m1 (q + 5*64, vl);
  vint8m1_t x6 = __riscv_vle8_v_i8m1 (q + 6*64, vl);
  vint8m1_t x7 = __riscv_vle8_v_i8m1 (q + 7*64, vl);
  vint8m1_t x8 = __riscv_vle8_v_i8m1 (q + 8*64, vl);
  vint8m1_t x9 = __riscv_vle8_v_i8m1 (q + 9*64, vl);
  vint8m1_t x10 = __riscv_vle8_v_i8m1 (q + 10*64, vl);
  vint8m1_t x11 = __riscv_vle8_v_i8m1 (q + 11*64, vl);
  vint8m1_t x12 = __riscv_vle8_v_i8m1 (q + 12*64, vl);
  vint8m1_t x13 = __riscv_vle8_v_i8m1 (q + 13*64, vl);
  vint8m1_t x14 = __riscv_vle8_v_i8m1 (q + 14*64, vl);
  vint8m1_t x15 = __riscv_vle8_v_i8m1 (q + 15*64, vl);
  vint8m1_t x16 = __riscv_vle8_v_i8m1 (q + 16*64, vl);
  vint8m1_t x17 = __riscv_vle8_v_i8m1 (q + 17*64, vl);
  vint8m1_t x18 = __riscv_vle8_v_i8m1 (q + 18*64, vl);
  vint8m1_t x19 = __riscv_vle8_v_i8m1 (q + 19*64, vl);
  vint8m1_t x20 = __riscv_vle8_v_i8m1 (q + 20*64, vl);
  vint8m1_t x21 = __riscv_vle8_v_i8m1 (q + 21*64, vl);
  vint8m1_t x22 = __riscv_vle8_v_i8m1 (q + 22*64, vl);
  vint8m1_t x23 = __riscv_vle8_v_i8m1 (q + 23*64, vl);
  vint8m1_t x24 = __riscv_vle8_v_i8m1 (q + 24*64, vl);
  vint8m1_t x25 = __riscv_vle8_v_i8m1 (q + 25*64, vl);
  vint8m1_t x26 = __riscv_vle8_v_i8m1 (q + 26*64, vl);
  vint8m1_t x27 = __riscv_vle8_v_i8m1 (q + 27*64, vl);
  vint8m1_t x28 = __riscv_vle8_v_i8m1 (q + 28*64, vl);
  vint8m1_t x29 = __riscv_vle8_v_i8m1 (q + 29*64, vl);
  vint8m1_t x30 = __riscv_vle8_v_i8m1 (q + 30*64, vl);
  vint8m1_t x31 = __riscv_vle8_v_i8m1 (q + 31*64, vl);
  vint8m1_t t = __riscv_vmttl_v_i8m1_m (m, p, ld, vl);
  __riscv_vse8_v_i8m1 (q + 1*64, x1, vl);
  __riscv_vse8_v_i8m1 (q + 2*64, x2, vl);
  __riscv_vse8_v_i8m1 (q + 3*64, x3, vl);
  __riscv_vse8_v_i8m1 (q + 4*64, x4, vl);
  __riscv_vse8_v_i8m1 (q + 5*64, x5, vl);
  __riscv_vse8_v_i8m1 (q + 6*64, x6, vl);
  __riscv_vse8_v_i8m1 (q + 7*64, x7, vl);
  __riscv_vse8_v_i8m1 (q + 8*64, x8, vl);
  __riscv_vse8_v_i8m1 (q + 9*64, x9, vl);
  __riscv_vse8_v_i8m1 (q + 10*64, x10, vl);
  __riscv_vse8_v_i8m1 (q + 11*64, x11, vl);
  __riscv_vse8_v_i8m1 (q + 12*64, x12, vl);
  __riscv_vse8_v_i8m1 (q + 13*64, x13, vl);
  __riscv_vse8_v_i8m1 (q + 14*64, x14, vl);
  __riscv_vse8_v_i8m1 (q + 15*64, x15, vl);
  __riscv_vse8_v_i8m1 (q + 16*64, x16, vl);
  __riscv_vse8_v_i8m1 (q + 17*64, x17, vl);
  __riscv_vse8_v_i8m1 (q + 18*64, x18, vl);
  __riscv_vse8_v_i8m1 (q + 19*64, x19, vl);
  __riscv_vse8_v_i8m1 (q + 20*64, x20, vl);
  __riscv_vse8_v_i8m1 (q + 21*64, x21, vl);
  __riscv_vse8_v_i8m1 (q + 22*64, x22, vl);
  __riscv_vse8_v_i8m1 (q + 23*64, x23, vl);
  __riscv_vse8_v_i8m1 (q + 24*64, x24, vl);
  __riscv_vse8_v_i8m1 (q + 25*64, x25, vl);
  __riscv_vse8_v_i8m1 (q + 26*64, x26, vl);
  __riscv_vse8_v_i8m1 (q + 27*64, x27, vl);
  __riscv_vse8_v_i8m1 (q + 28*64, x28, vl);
  __riscv_vse8_v_i8m1 (q + 29*64, x29, vl);
  __riscv_vse8_v_i8m1 (q + 30*64, x30, vl);
  __riscv_vse8_v_i8m1 (q + 31*64, x31, vl);
  __riscv_vse8_v_i8m1 (q, t, vl);
}

/* { dg-final { scan-assembler-times {\mvmtl\.v\tv[0-9]+,[^\n]*,v0\.t} 1 } } */
/* { dg-final { scan-assembler-times {\mvmttl\.v\tv[0-9]+,[^\n]*,v0\.t} 1 } } */
/* { dg-final { scan-assembler-not {\mvmtt?l\.v\tv0,[^\n]*v0\.t} } } */
