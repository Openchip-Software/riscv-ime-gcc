/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb_zvfh -mabi=lp64d -O2" } */

/* Regression: in the runtime-VLEN path the OFP8 mmacc expanders used
   to pass altfmt_a/altfmt_b/altfmt = (false, false, false) regardless
   of the per-instance UNSPEC, so all OFP8 altfmt combinations emitted
   the same vtype constant.  At runtime the vsetvl would write a vtype
   without the altfmt bits set, the WARL readback would still match
   (lambda is the only field probed), and the consumer would silently
   compute in the wrong format.

   Static-VLEN compilation already encoded altfmt correctly via the
   ime_fp_altfmt MD attribute (riscv_extract_ime_requirements in
   riscv-vsetvl.cc).  This test exercises the runtime-VLEN path and
   pins down that it now agrees: build static_bits using altfmt_A
   (vtype bit 59), altfmt_B (bit 58) and altfmt (bit 8) according to
   the OFP8 UNSPEC.

   At xlen=64, altfmt_A maps to bit 59 and altfmt_B to bit 58.  GCC
   always synthesises that pair as `li tmp, K; slli tmp, tmp, 58'.
   The 58-shift is therefore a unique fingerprint of "at least one of
   altfmt_A / altfmt_B is set in static_bits".  An all-E4M3 W=1
   kernel must NOT emit a `slli ...,58'; an all-E5M2 W=1 kernel
   must.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Baseline: all inputs and acc E4M3 -> altfmt = (0, 0, 0).  */
vfloat8e4m3m1_t k_e4m3 (vfloat8e4m3m1_t a, vfloat8e4m3m1_t s1,
			vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m1_f8e4m3m1 (a, s1, s2, vl); }

/* All E5M2 -> altfmt = (1, 1, 1).  */
vfloat8e5m2m1_t k_e5m2 (vfloat8e5m2m1_t a, vfloat8e5m2m1_t s1,
			vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m1_f8e5m2m1 (a, s1, s2, vl); }

/* W=2 OFP8 -> FP16, all E5M2 inputs (acc altfmt = 0 since FP16)
   -> altfmt = (1, 1, 0).  */
vfloat16m1_t k_w2_fp16_e5m2 (vfloat16m1_t a, vfloat8e5m2m1_t s1,
			     vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m1_f8e5m2m1 (a, s1, s2, vl); }

/* W=4 OFP8 -> FP32, all E5M2 inputs -> altfmt = (1, 1, 0).  */
vfloat32m1_t k_w4_e5m2 (vfloat32m1_t a, vfloat8e5m2m1_t s1,
			vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfqwmmacc_vv_f32m1_f8e5m2m1 (a, s1, s2, vl); }

/* W=8 OFP8 -> FP64, all E5M2 inputs -> altfmt = (1, 1, 0).  */
vfloat64m1_t k_w8_e5m2 (vfloat64m1_t a, vfloat8e5m2m1_t s1,
			vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vf8wmmacc_vv_f64m1_f8e5m2m1 (a, s1, s2, vl); }

/* Four kernels above set altfmt_A/altfmt_B in static_bits, each
   emitting one `slli ...,58' instruction.  The all-E4M3 kernel emits
   none.  */
/* { dg-final { scan-assembler-times {\tslli\t[^,]+,[^,]+,58\n} 4 } } */
