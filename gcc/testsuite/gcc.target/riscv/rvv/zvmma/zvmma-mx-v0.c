/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The microscaled multiply-accumulates read their block scales from v0,
   which vd, vs1 and vs2 may not overlap, even when nearly every other
   vector register is live.  */

typedef __SIZE_TYPE__ size_t;
typedef __INT8_TYPE__ int8_t;
#pragma riscv intrinsic "vector"

#define LD(i) vint8m1_t a##i = __riscv_vle8_v_i8m1 (in + (i) * 32, 32)
#define MX(i, j, k) \
  c##i = __riscv_vfqimmacc_vv_f32m2_i8m1_bs32 (c##i, a##j, a##k, s, vl)

void
f (float *out, const int8_t *in, vuint16m1_t s, size_t vl)
{
  vfloat32m2_t c0 = __riscv_vle32_v_f32m2 (out, 16);
  vfloat32m2_t c1 = __riscv_vle32_v_f32m2 (out + 16, 16);
  vfloat32m2_t c2 = __riscv_vle32_v_f32m2 (out + 32, 16);
  vfloat32m2_t c3 = __riscv_vle32_v_f32m2 (out + 48, 16);
  vfloat32m2_t c4 = __riscv_vle32_v_f32m2 (out + 64, 16);
  vfloat32m2_t c5 = __riscv_vle32_v_f32m2 (out + 80, 16);
  LD (0); LD (1); LD (2); LD (3); LD (4); LD (5); LD (6); LD (7);
  LD (8); LD (9); LD (10); LD (11); LD (12); LD (13); LD (14); LD (15);
  LD (16); LD (17);
  MX (0, 0, 1); MX (1, 2, 3); MX (2, 4, 5); MX (3, 6, 7); MX (4, 8, 9);
  MX (5, 10, 11); MX (0, 12, 13); MX (1, 14, 15); MX (2, 16, 17);
  MX (3, 1, 2); MX (4, 3, 4); MX (5, 5, 6); MX (0, 7, 8); MX (1, 9, 10);
  MX (2, 11, 12); MX (3, 13, 14); MX (4, 15, 16); MX (5, 17, 0);
  __riscv_vse32_v_f32m2 (out, c0, 16);
  __riscv_vse32_v_f32m2 (out + 16, c1, 16);
  __riscv_vse32_v_f32m2 (out + 32, c2, 16);
  __riscv_vse32_v_f32m2 (out + 48, c3, 16);
  __riscv_vse32_v_f32m2 (out + 64, c4, 16);
  __riscv_vse32_v_f32m2 (out + 80, c5, 16);
}

/* { dg-final { scan-assembler-times {\tvfqimmacc\.vv\t} 18 } } */
/* { dg-final { scan-assembler-not {\tvfqimmacc\.vv\tv0,} } } */
/* { dg-final { scan-assembler-not {\tvfqimmacc\.vv\t[^\n]*,v0,} } } */
