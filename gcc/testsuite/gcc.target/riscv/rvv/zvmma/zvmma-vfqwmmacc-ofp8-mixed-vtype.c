/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* W=4 vfqwmmacc OFP8 mixed-input variants: each (altfmt_A, altfmt_B)
   pair must produce a distinct vtype constant.  This is what
   distinguishes E4M3 x E5M2 vs E5M2 x E4M3 at the QEMU dispatch table.

   At VLEN=512, m1, SEW=32: lambda^2 = 16, lambda = 4 -> encoding 0b011.
   Static bits: SEW=32 (vsew=010) << 3 = 0x10, LMUL=m1 = 0,
   ta + ma = 0xC0.  Total static = 0xD0.

   E4M3 x E4M3 (a=0, b=0): 0x3000_0000_0000_00D0 = 3458764513820541136
   E4M3 x E5M2 (a=0, b=1): 0x3400_0000_0000_00D0 = 3746994889972252880
   E5M2 x E4M3 (a=1, b=0): 0x3800_0000_0000_00D0 = 4035225266123964624
   E5M2 x E5M2 (a=1, b=1): 0x3C00_0000_0000_00D0 = 4323455642275676368  */

vfloat32m1_t e4m3_e4m3 (vfloat32m1_t a, vfloat8e4m3m1_t s1,
			vfloat8e4m3m1_t s2, size_t vl)
{
  return __riscv_vfqwmmacc_vv_f32m1_f8e4m3m1 (a, s1, s2, vl);
}

vfloat32m1_t e4m3_e5m2 (vfloat32m1_t a, vfloat8e4m3m1_t s1,
			vfloat8e5m2m1_t s2, size_t vl)
{
  return __riscv_vfqwmmacc_vv_f32m1_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl);
}

vfloat32m1_t e5m2_e4m3 (vfloat32m1_t a, vfloat8e5m2m1_t s1,
			vfloat8e4m3m1_t s2, size_t vl)
{
  return __riscv_vfqwmmacc_vv_f32m1_f8e5m2m1_f8e4m3m1 (a, s1, s2, vl);
}

vfloat32m1_t e5m2_e5m2 (vfloat32m1_t a, vfloat8e5m2m1_t s1,
			vfloat8e5m2m1_t s2, size_t vl)
{
  return __riscv_vfqwmmacc_vv_f32m1_f8e5m2m1 (a, s1, s2, vl);
}

/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541136\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3746994889972252880\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3602879701896397008\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3891110078048108752\n} 1 } } */
/* { dg-final { scan-assembler-not {ebreak} } } */
