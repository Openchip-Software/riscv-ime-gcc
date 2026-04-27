/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb_zvfh_zvfbfmin -mabi=lp64d -O3" } */

/* Regression test for gap #8: at -O3 with multiple widening callsites
   in a single function, the vsetvl pass tripped a SEW assertion in
   riscv_extract_ime_requirements when its mmacc-geometry walker
   left recog_data populated with the last visited mmacc's operands.
   Before the fix this ICEd; after, it must compile.  */

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#pragma riscv intrinsic "vector"

extern unsigned char A[], B[], C[];

#define DEFINE_PATH(NAME, MNEMONIC, IFMT, IVT, ATYPE, AFMT, AVT, ASEW)        \
void NAME (size_t vl, ptrdiff_t a_ld, ptrdiff_t b_ld, ptrdiff_t c_ld) {	      \
  IVT##m1_t a = __riscv_vmtl_v_##IFMT##m1 ((const void *)A, a_ld, vl);	      \
  IVT##m1_t b = __riscv_vmtl_v_##IFMT##m1 ((const void *)B, b_ld, vl);	      \
  AVT##m1_t acc = __riscv_vreinterpret_v_u##ASEW##m1_##AFMT##m1 (	      \
      __riscv_vmv_v_x_u##ASEW##m1 (0, vl));				      \
  acc = __riscv_##MNEMONIC##_vv_##AFMT##m1##_##IFMT##m1 (acc, a, b, vl);      \
  __riscv_vmts_v_##AFMT##m1 ((ATYPE *)C, c_ld, acc, vl);		      \
}

DEFINE_PATH (test_f32_f16,  vfwmmacc,  f16,  vfloat16,  float,  f32, vfloat32, 32)
DEFINE_PATH (test_f32_bf16, vfwmmacc,  bf16, vbfloat16, float,  f32, vfloat32, 32)
DEFINE_PATH (test_f64_f32,  vfwmmacc,  f32,  vfloat32,  double, f64, vfloat64, 64)
DEFINE_PATH (test_f64_f16,  vfqwmmacc, f16,  vfloat16,  double, f64, vfloat64, 64)
DEFINE_PATH (test_f64_bf16, vfqwmmacc, bf16, vbfloat16, double, f64, vfloat64, 64)

/* The aggregator is the historical ICE trigger: pulling all five
   widening variants into one function caused recog_data to be
   shared across mmacc-geometry walks for sibling tile insns.  */
void run_all (size_t vl, ptrdiff_t a_ld, ptrdiff_t b_ld, ptrdiff_t c_ld)
{
  test_f32_f16  (vl, a_ld, b_ld, c_ld);
  test_f32_bf16 (vl, a_ld, b_ld, c_ld);
  test_f64_f32  (vl, a_ld, b_ld, c_ld);
  test_f64_f16  (vl, a_ld, b_ld, c_ld);
  test_f64_bf16 (vl, a_ld, b_ld, c_ld);
}

unsigned char A[4096], B[4096], C[4096];

/* At -O3 each test_* function is emitted standalone and also inlined
   into run_all, so each callsite contributes two vmmacc instructions.  */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv\t}  6 } } */
/* { dg-final { scan-assembler-times {\tvfqwmmacc\.vv\t} 4 } } */
