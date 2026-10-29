/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvvxofp8fp16mm_zvvxi4fp32mm_zvfh -mabi=lp64d -O2" } */

/* Each microscaled multiply-accumulate requires the extension of its
   block size; the BS16 one implies the BS32 one, not the reverse.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

vfloat16m1_t
ofp8_bs32 (vfloat16m1_t vd, vfloat8e4m3m1_t a, vfloat8e4m3m1_t b,
	   vuint16m1_t s, size_t vl)
{
  return __riscv_vfwmmacc_scaled_vv_f16m1_f8e4m3m1_bs32 (vd, a, b, s, vl);
}

vfloat16m1_t
ofp8_bs16 (vfloat16m1_t vd, vfloat8e4m3m1_t a, vfloat8e4m3m1_t b,
	   vuint16m1_t s, size_t vl)
{
  return __riscv_vfwmmacc_scaled_vv_f16m1_f8e4m3m1_bs16 (vd, a, b, s, vl); /* { dg-error "requires the 'zvvxnofp8fp16mm' ISA extension" } */
}

vfloat32m1_t
i4_bs32 (vfloat32m1_t vd, vint4m1_t a, vint4m1_t b, vuint16m1_t s, size_t vl)
{
  return __riscv_vf8wimmacc_vv_f32m1_i4m1_bs32 (vd, a, b, s, vl);
}

vfloat16m1_t
i4_fp16 (vfloat16m1_t vd, vint4m1_t a, vint4m1_t b, vuint16m1_t s, size_t vl)
{
  return __riscv_vfqimmacc_vv_f16m1_i4m1_bs32 (vd, a, b, s, vl); /* { dg-error "requires the 'zvvxi4fp16mm' ISA extension" } */
}
