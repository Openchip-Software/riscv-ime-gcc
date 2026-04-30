/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl128b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* Stage 3a: W=1 vfmmacc OFP8 -> OFP8.  All 8 (acc, vs1, vs2) altfmt
   combinations must produce distinct vtype constants.

   At VLEN=128, m1, SEW=8: lambda^2 = 128/8 = 16, lambda = 4 ->
   encoding 0b011.  Static bits: SEW=8 (vsew=000) << 3 = 0, LMUL=m1 = 0,
   ta + ma = 0xC0.  Total static (no altfmt) = 0xC0.

   altfmt is the union of three vtype bits:
     bit 8       = altfmt   (acc/output format)
     bit 58 (-6) = altfmt_B (vs2 input format)
     bit 59 (-5) = altfmt_A (vs1 input format)

   E4M3 = 0, E5M2 = 1.  Tuple (A, B, F):

     (0,0,0) acc E4M3 / E4M3 / E4M3  -> 0x3000_0000_0000_00C0
     (0,1,0) acc E4M3 / E4M3 / E5M2  -> 0x3400_0000_0000_00C0
     (1,0,0) acc E4M3 / E5M2 / E4M3  -> 0x3800_0000_0000_00C0
     (1,1,0) acc E4M3 / E5M2 / E5M2  -> 0x3C00_0000_0000_00C0
     (0,0,1) acc E5M2 / E4M3 / E4M3  -> 0x3000_0000_0000_01C0
     (0,1,1) acc E5M2 / E4M3 / E5M2  -> 0x3400_0000_0000_01C0
     (1,0,1) acc E5M2 / E5M2 / E4M3  -> 0x3800_0000_0000_01C0
     (1,1,1) acc E5M2 / E5M2 / E5M2  -> 0x3C00_0000_0000_01C0  */

vfloat8e4m3m1_t a000 (vfloat8e4m3m1_t a, vfloat8e4m3m1_t s1,
		      vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m1_f8e4m3m1 (a, s1, s2, vl); }

vfloat8e4m3m1_t a010 (vfloat8e4m3m1_t a, vfloat8e4m3m1_t s1,
		      vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m1_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl); }

vfloat8e4m3m1_t a100 (vfloat8e4m3m1_t a, vfloat8e5m2m1_t s1,
		      vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m1_f8e5m2m1_f8e4m3m1 (a, s1, s2, vl); }

vfloat8e4m3m1_t a110 (vfloat8e4m3m1_t a, vfloat8e5m2m1_t s1,
		      vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl); }

vfloat8e5m2m1_t a001 (vfloat8e5m2m1_t a, vfloat8e4m3m1_t s1,
		      vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m1_f8e4m3m1 (a, s1, s2, vl); }

vfloat8e5m2m1_t a011 (vfloat8e5m2m1_t a, vfloat8e4m3m1_t s1,
		      vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m1_f8e4m3m1_f8e5m2m1 (a, s1, s2, vl); }

vfloat8e5m2m1_t a101 (vfloat8e5m2m1_t a, vfloat8e5m2m1_t s1,
		      vfloat8e4m3m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m1_f8e5m2m1_f8e4m3m1 (a, s1, s2, vl); }

vfloat8e5m2m1_t a111 (vfloat8e5m2m1_t a, vfloat8e5m2m1_t s1,
		      vfloat8e5m2m1_t s2, size_t vl)
{ return __riscv_vfmmacc_vv_f8e5m2m1_f8e5m2m1 (a, s1, s2, vl); }

/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541120\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3746994889972252864\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4035225266123964608\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4323455642275676352\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3458764513820541376\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,3746994889972253120\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4035225266123964864\n} 1 } } */
/* { dg-final { scan-assembler-times {\tli\tt0,4323455642275676608\n} 1 } } */
/* { dg-final { scan-assembler-not {ebreak} } } */
