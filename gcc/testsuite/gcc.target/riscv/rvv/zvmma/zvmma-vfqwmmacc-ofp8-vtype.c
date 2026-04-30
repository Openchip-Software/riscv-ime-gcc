/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* The two OFP8 same-format variants must produce DIFFERENT vtype
   constants -- E4M3 has altfmt_A=altfmt_B=0, E5M2 has both set.
   Before the ime_fp_altfmt attribute was wired up, both emitted the
   same vtype (altfmt bits both clear) and QEMU dispatched both as
   E4M3 -- E5M2 bit patterns silently produced wrong results.  */

vfloat32m1_t e4m3 (vfloat32m1_t a, vfloat8e4m3m1_t s1, vfloat8e4m3m1_t s2,
		   size_t vl)
{
  return __riscv_vfqwmmacc_vv_f32m1_f8e4m3m1 (a, s1, s2, vl);
}

vfloat32m1_t e5m2 (vfloat32m1_t a, vfloat8e5m2m1_t s1, vfloat8e5m2m1_t s2,
		   size_t vl)
{
  return __riscv_vfqwmmacc_vv_f32m1_f8e5m2m1 (a, s1, s2, vl);
}

/* W=4 vfqwmmacc OFP8 -> FP32 at VLEN=512, m1.  lambda^2 = 512/(32*1)
   = 16 (perfect square), lambda = 4 -> encoding 0b011.
   Lower static bits: SEW=32 (vsew=010) << 3 = 0x10, LMUL=m1 = 0,
   ta + ma = 0xC0.  Total static = 0xD0.

   E4M3 vtype = 0x3000_0000_0000_00D0 = 3458764513820541136  (a=0, b=0)
   E5M2 vtype = 0x3C00_0000_0000_00D0 = 4323455642275676368  (a=1, b=1)  */
/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541136\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4323455642275676368\n} 1 } } */

/* No perfect-square trap: VLEN=512 yields perfect-square lambda^2 for
   both static and runtime paths.  */
/* { dg-final { scan-assembler-not {ebreak} } } */
