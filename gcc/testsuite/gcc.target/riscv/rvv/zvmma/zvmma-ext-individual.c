/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvvi8i32mm_zvvxi8fp32mm -mabi=lp64d -O2" } */

/* An individual extension enables only the multiply-accumulates of its
   input and accumulator formats; it implies no family extension.  */

#if defined (__riscv_zvvmm) || defined (__riscv_zvvfmm) \
    || defined (__riscv_zvvi16mm) || defined (__riscv_zvvofp8fp32mm)
#error "unexpected extension"
#endif

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vint32m1_t
i8i32 (vint32m1_t vd, vint8m1_t a, vint8m1_t b, size_t vl)
{
  return __riscv_vqmmacc_vv_i32m1 (vd, a, b, vl);
}

vint16m1_t
i16 (vint16m1_t vd, vint16m1_t a, vint16m1_t b, size_t vl)
{
  return __riscv_vmmacc_vv_i16m1 (vd, a, b, vl); /* { dg-error "requires the 'zvvi16mm' ISA extension" } */
}

vfloat32m1_t
mxi8 (vfloat32m1_t vd, vint8m1_t a, vint8m1_t b, vuint16m1_t s, size_t vl)
{
  return __riscv_vfqimmacc_vv_f32m1_i8m1_bs32 (vd, a, b, s, vl);
}

vfloat32m1_t
mxi8_bs16 (vfloat32m1_t vd, vint8m1_t a, vint8m1_t b, vuint16m1_t s,
	   size_t vl)
{
  return __riscv_vfqimmacc_vv_f32m1_i8m1_bs16 (vd, a, b, s, vl); /* { dg-error "requires the 'zvvxni8fp32mm' ISA extension" } */
}

vfloat32m1_t
mxofp8 (vfloat32m1_t vd, vfloat8e4m3m1_t a, vfloat8e4m3m1_t b, vuint16m1_t s,
	size_t vl)
{
  return __riscv_vfqmmacc_scaled_vv_f32m1_f8e4m3m1_bs32 (vd, a, b, s, vl); /* { dg-error "requires the 'zvvxofp8fp32mm' ISA extension" } */
}
