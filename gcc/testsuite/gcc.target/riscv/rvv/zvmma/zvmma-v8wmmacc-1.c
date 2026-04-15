/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl1024b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* W=8 v8wmmacc.vv: only DI accumulators have a valid oct-truncated
   (QI) input type.  At VLEN=1024 with SEW=64 and MUL_C=1 the
   required lambda is 4 (supported).  */

vint64m1_t
ew_ss (vint64m1_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_v8wmmacc_vv_i64m1 (vd, a, b, vl);
}

vuint64m1_t
ew_uu (vuint64m1_t vd, vuint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_v8wmmacc_vv_u64m1 (vd, a, b, vl);
}

vint64m1_t
ew_su (vint64m1_t vd, vint8m1_t a, vuint8m1_t b, size_t vl)
{
  return __riscv_v8wmmacc_vv_i64m1_su (vd, a, b, vl);
}

vint64m1_t
ew_us (vint64m1_t vd, vuint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_v8wmmacc_vv_i64m1_us (vd, a, b, vl);
}

/* { dg-final { scan-assembler-times {\tv8wmmacc\.vv\t} 4 } } */
/* { dg-final { scan-assembler-times {\tvsetvl\t} 4 } } */
