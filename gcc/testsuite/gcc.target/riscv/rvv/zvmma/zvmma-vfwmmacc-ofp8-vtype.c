/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl256b_zvfh_zvfbfmin -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Stage 4: W=2 vfwmmacc OFP8 -> fp16 / bf16.  Eight (acc, vs1, vs2)
   altfmt combinations across the 2 acc formats (fp16 vs bf16) and the
   2x2 OFP8 input cube must produce eight distinct vtype constants.

   VLEN=256, m1, SEW=16 (W=2): lambda^2 = 256/16 = 16, lambda = 4 ->
   encoding 0b011.  Static bits: SEW=16 (vsew=001) << 3 = 0x08, LMUL=m1
   = 0, ta + ma = 0xC0.  Total static (no altfmt) = 0xC8.

   acc altfmt at bit 8: 0 -> fp16, 1 -> bf16.  altfmt_A at bit 59,
   altfmt_B at bit 58.

   fp16 acc (lower bits 0x00C8):
     (A=0,B=0): 0x3000_0000_0000_00C8 = 3458764513820541128
     (A=0,B=1): 0x3400_0000_0000_00C8 = 3746994889972252872
     (A=1,B=0): 0x3800_0000_0000_00C8 = 4035225266123964616
     (A=1,B=1): 0x3C00_0000_0000_00C8 = 4323455642275676360
   bf16 acc (lower bits 0x01C8):
     (A=0,B=0): 0x3000_0000_0000_01C8 = 3458764513820541384
     (A=0,B=1): 0x3400_0000_0000_01C8 = 3746994889972253128
     (A=1,B=0): 0x3800_0000_0000_01C8 = 4035225266123964872
     (A=1,B=1): 0x3C00_0000_0000_01C8 = 4323455642275676616  */

vfloat16m1_t fp16_e4m3_e4m3 (vfloat16m1_t a, vfloat8e4m3m1_t s1,
			     vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m1_f8e4m3m1 (a, s1, s2, vl); }

vfloat16m1_t fp16_e4m3_e5m2 (vfloat16m1_t a, vfloat8e4m3m1_t s1,
			     vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m1_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl); }

vfloat16m1_t fp16_e5m2_e4m3 (vfloat16m1_t a, vfloat8e5m2m1_t s1,
			     vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m1_f8e5m2m1_f8e4m3m1 (a, s1, s2, vl); }

vfloat16m1_t fp16_e5m2_e5m2 (vfloat16m1_t a, vfloat8e5m2m1_t s1,
			     vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_f16m1_f8e5m2m1 (a, s1, s2, vl); }

vbfloat16m1_t bf16_e4m3_e4m3 (vbfloat16m1_t a, vfloat8e4m3m1_t s1,
			      vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_bf16m1_f8e4m3m1 (a, s1, s2, vl); }

vbfloat16m1_t bf16_e4m3_e5m2 (vbfloat16m1_t a, vfloat8e4m3m1_t s1,
			      vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_bf16m1_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl); }

vbfloat16m1_t bf16_e5m2_e4m3 (vbfloat16m1_t a, vfloat8e5m2m1_t s1,
			      vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_bf16m1_f8e5m2m1_f8e4m3m1 (a, s1, s2, vl); }

vbfloat16m1_t bf16_e5m2_e5m2 (vbfloat16m1_t a, vfloat8e5m2m1_t s1,
			      vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfwmmacc_vv_bf16m1_f8e5m2m1 (a, s1, s2, vl); }

/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541128\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3746994889972252872\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4035225266123964616\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4323455642275676360\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541384\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3746994889972253128\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4035225266123964872\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4323455642275676616\n} 1 } } */
/* { dg-final { scan-assembler-not {ebreak} } } */
