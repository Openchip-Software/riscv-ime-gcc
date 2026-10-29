/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma_zvfh_zvfbfmin_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

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

/* At VLEN = 256 the full vtype is a constant: e32, m1, ta, ma, lambda = 2,
   with bs clear (0x20000000000000d0) for _bs32 and set
   (0x28000000000000d0) for _bs16.  */
/* { dg-final { scan-assembler-times {\mli\tt0,2305843009213694160\M} 1 } } */
/* { dg-final { scan-assembler-times {\mli\tt0,2882303761517117648\M} 1 } } */
