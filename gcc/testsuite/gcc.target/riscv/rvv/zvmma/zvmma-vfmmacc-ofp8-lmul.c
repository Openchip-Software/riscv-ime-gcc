/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl128b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Stage 3b: W=1 vfmmacc OFP8 -> OFP8 with non-trivial acc LMUL.
   Verifies that the m2/m4/m8 acc types are registered and that the
   resulting vtype carries the correct lambda encoding for each
   (VLEN, MUL_C) combination.

   At VLEN=128, SEW=8 (W=1):
     m1 acc (MUL_C=1): lambda^2 = 128/8 = 16, lambda = 4 -> enc 0b011
     m4 acc (MUL_C=4): lambda^2 = 128/32 = 4, lambda = 2 -> enc 0b010
   m2 (MUL_C=2) gives lambda^2 = 8 -> not perfect square; m8 likewise.
   Use VLEN=128 with m1 / m4 only here; m2 / m8 land on different
   VLENs and live in their own dedicated tests.  */

vfloat8e4m3m1_t same_m1 (vfloat8e4m3m1_t a, vfloat8e4m3m1_t s1,
			 vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m1_f8e4m3m1 (a, s1, s2, vl); }

vfloat8e4m3m4_t same_m4 (vfloat8e4m3m4_t a, vfloat8e4m3m1_t s1,
			 vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m4_f8e4m3m1 (a, s1, s2, vl); }

vfloat8e4m3m4_t mixed_m4 (vfloat8e4m3m4_t a, vfloat8e5m2m1_t s1,
			  vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m4_f8e5m2m1_f8e4m3m1 (a, s1, s2, vl); }

/* m1 same-fmt E4M3:    0x3000_0000_0000_00C0 = 3458764513820541120
   m4 same-fmt E4M3:    0x2000_0000_0000_00C0 = 2305843009213694144
   m4 mixed (A=1,B=0):  0x2800_0000_0000_00C0 = 2882303761517117632  */
/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541120\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,2305843009213694144\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,2594073385365405888\n} 1 } } */
/* { dg-final { scan-assembler-not {ebreak} } } */
