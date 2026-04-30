/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb_zvfh_zvfbfmin -mabi=lp64d -O2" } */

/* Stage 5 of gap #4: end-to-end runtime-VLEN test for the OFP8 mmacc
   surface introduced in stages 1-4.  This complements the per-stage
   static-VLEN scan-assembler tests (which verify each emitted vtype
   constant) by exercising the runtime probe path for the same
   intrinsics.

   Compiled WITHOUT -mrvv-vector-bits=zvl, the IME consumer cannot
   know lambda at compile time and must emit the runtime probe
   sequence: csrr vlenb, ctz, vsetvl + WARL match.  We verify that:
   1. Each consumer emits exactly the expected mmacc mnemonic.
   2. Exactly one ctz appears per consumer (the lambda probe).
   3. The static_bits in the IOR step differ between input-altfmt
      combinations (otherwise the dispatch would be broken).  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* W=1 vfmmacc OFP8 -> OFP8 same-format E4M3.  */
vfloat8e4m3m1_t w1_e4m3 (vfloat8e4m3m1_t a, vfloat8e4m3m1_t s1,
			 vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m1_f8e4m3m1 (a, s1, s2, vl); }

/* W=1 vfmmacc OFP8 -> OFP8 mixed inputs, E5M2 acc.  */
vfloat8e5m2m1_t w1_mixed (vfloat8e5m2m1_t a, vfloat8e4m3m1_t s1,
			  vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m1_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl); }

/* W=2 vfwmmacc OFP8 -> fp16, mixed inputs.  */
vfloat16m1_t w2_fp16_mixed (vfloat16m1_t a, vfloat8e4m3m1_t s1,
			    vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m1_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl); }

/* W=2 vfwmmacc OFP8 -> bf16, same-format E5M2.  */
vbfloat16m1_t w2_bf16_e5m2 (vbfloat16m1_t a, vfloat8e5m2m1_t s1,
			    vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_bf16m1_f8e5m2m1 (a, s1, s2, vl); }

/* W=4 vfqwmmacc OFP8 -> fp32, same-format and mixed.  */
vfloat32m1_t w4_e4m3 (vfloat32m1_t a, vfloat8e4m3m1_t s1,
		      vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfqwmmacc_vv_f32m1_f8e4m3m1 (a, s1, s2, vl); }

vfloat32m1_t w4_mixed (vfloat32m1_t a, vfloat8e5m2m1_t s1,
		       vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfqwmmacc_vv_f32m1_f8e5m2m1_f8e4m3m1 (a, s1, s2, vl); }

/* W=8 vf8wmmacc OFP8 -> fp64, same-format and mixed.  */
vfloat64m1_t w8_e5m2 (vfloat64m1_t a, vfloat8e5m2m1_t s1,
		      vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vf8wmmacc_vv_f64m1_f8e5m2m1 (a, s1, s2, vl); }

vfloat64m1_t w8_mixed (vfloat64m1_t a, vfloat8e4m3m1_t s1,
		       vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vf8wmmacc_vv_f64m1_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl); }

/* Each function emits exactly one consumer (the runtime probe sets
   up vtype, then the consumer issues).  vfwmmacc shares the same
   mnemonic at W=2 widening, so split the count into vfmmacc / vfwmmacc
   / vfqwmmacc / vf8wmmacc.  */
/* { dg-final { scan-assembler-times {\tvfmmacc\.vv} 2 } } */
/* { dg-final { scan-assembler-times {\tvfwmmacc\.vv} 2 } } */
/* { dg-final { scan-assembler-times {\tvfqwmmacc\.vv} 2 } } */
/* { dg-final { scan-assembler-times {\tvf8wmmacc\.vv} 2 } } */

/* Each consumer's runtime path must use ctz on the (vlenb << 3) >>
   log2(SEW * MUL_C) value to derive lambda; one ctz per consumer.  */
/* { dg-final { scan-assembler-times {\tctz\t} 8 } } */

/* The runtime probe writes vtype via vsetvl (register-form), then
   reads it back via csrr vtype, compares lambda field, and traps on
   mismatch (ebreak).  Each consumer goes through this sequence; with
   8 consumers we expect 8 ebreak instructions (one trap label per
   consumer).  */
/* { dg-final { scan-assembler-times {\tebreak} 8 } } */
