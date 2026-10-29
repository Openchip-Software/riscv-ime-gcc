/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma_zvfh_zvfbfmin -mabi=lp64d -O2" } */

/* The vtype bs bit (bit 59 on RV64) selects block size 16 when set and
   block size 32 when clear.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vfloat32m2_t
bs32 (vfloat32m2_t vd, vint8m1_t a, vint8m1_t b, vuint16m1_t sc, size_t vl)
{
  return __riscv_vfqimmacc_vv_f32m2_i8m1_bs32 (vd, a, b, sc, vl);
}

vfloat32m2_t
bs16 (vfloat32m2_t vd, vint8m1_t a, vint8m1_t b, vuint16m1_t sc, size_t vl)
{
  return __riscv_vfqimmacc_vv_f32m2_i8m1_bs16 (vd, a, b, sc, vl);
}

/* With a runtime lambda, only the _bs16 call builds the bs bit.  */
/* { dg-final { scan-assembler-times {\mslli\t[a-z0-9]+,[a-z0-9]+,59\M} 1 } } */
