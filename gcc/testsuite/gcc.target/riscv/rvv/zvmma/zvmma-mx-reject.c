/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Block size 16 needs W * LMUL <= SEW, so the microscaled forms that
   violate it do not exist.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* W=8, LMUL=8 and SEW=32: only block size 32.  */
vfloat32m2_t
lm8_bs32 (vfloat32m2_t vd, vfloat4e2m1m8_t a, vfloat4e2m1m8_t b,
	  vuint16m1_t s, size_t vl)
{
  return __riscv_vf8wmmacc_scaled_vv_f32m2_f4e2m1m8_lm8_bs32 (vd, a, b, s, vl);
}

vfloat32m2_t
lm8_bs16 (vfloat32m2_t vd, vfloat4e2m1m8_t a, vfloat4e2m1m8_t b,
	  vuint16m1_t s, size_t vl)
{
  return __riscv_vf8wmmacc_scaled_vv_f32m2_f4e2m1m8_lm8_bs16 (vd, a, b, s, vl); /* { dg-error "implicit declaration" } */
  /* { dg-error "incompatible types" "" { target *-*-* } .-1 } */
}
