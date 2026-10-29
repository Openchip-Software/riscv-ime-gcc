/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The lambda established on entry is killed by the e16 vsetvli of the
   store, many instructions before the multiply-accumulate in the same
   block.  The IME vsetvl before the multiply-accumulate must still be
   emitted.  */

#include <stddef.h>
#pragma riscv intrinsic "vector"

vint32m1_t
f (vint32m1_t acc, vint8m1_t a, vint8m1_t b, vint16m2_t x, short *q,
   volatile int *p, size_t vl, int c)
{
  __riscv_vsetlambda (4);
  if (c)
    {
      __riscv_vse16_v_i16m2 (q, x, vl);
      p[0] = 0;
      p[1] = 1;
      p[2] = 2;
      p[3] = 3;
      p[4] = 4;
      p[5] = 5;
      p[6] = 6;
      p[7] = 7;
      p[8] = 8;
      p[9] = 9;
      p[10] = 10;
      p[11] = 11;
      p[12] = 12;
      p[13] = 13;
      p[14] = 14;
      p[15] = 15;
      p[16] = 16;
      p[17] = 17;
      p[18] = 18;
      p[19] = 19;
      p[20] = 20;
      p[21] = 21;
      p[22] = 22;
      p[23] = 23;
      p[24] = 24;
      p[25] = 25;
      p[26] = 26;
      p[27] = 27;
      p[28] = 28;
      p[29] = 29;
      p[30] = 30;
      p[31] = 31;
      p[32] = 32;
      p[33] = 33;
      p[34] = 34;
      p[35] = 35;
      p[36] = 36;
      p[37] = 37;
      p[38] = 38;
      p[39] = 39;
      p[40] = 40;
      p[41] = 41;
      p[42] = 42;
      p[43] = 43;
      p[44] = 44;
      p[45] = 45;
      p[46] = 46;
      p[47] = 47;
      p[48] = 48;
      p[49] = 49;
      p[50] = 50;
      p[51] = 51;
      p[52] = 52;
      p[53] = 53;
      p[54] = 54;
      p[55] = 55;
      p[56] = 56;
      p[57] = 57;
      p[58] = 58;
      p[59] = 59;
      p[60] = 60;
      p[61] = 61;
      p[62] = 62;
      p[63] = 63;
      p[64] = 64;
      p[65] = 65;
      p[66] = 66;
      p[67] = 67;
      p[68] = 68;
      p[69] = 69;
      p[70] = 70;
      p[71] = 71;
      p[72] = 72;
      p[73] = 73;
      p[74] = 74;
      p[75] = 75;
      p[76] = 76;
      p[77] = 77;
      p[78] = 78;
      p[79] = 79;
      acc = __riscv_vqmmacc_vv_i32m1 (acc, a, b, vl);
    }
  return acc;
}

/* One vsetvl for the lambda install and one for the multiply-accumulate.  */
/* { dg-final { scan-assembler-times {\mvsetvl\M} 2 } } */
