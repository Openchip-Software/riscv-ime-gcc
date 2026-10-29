/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvfh_zvl128b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* The microscaled forms (vm=0) need SEW * lambda >= 16.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

/* SEW=8 and EMUL_C=16 at VLEN=128: lambda=1.  */
vfloat8e4m3m8_t
lambda1 (vfloat8e4m3m8_t lo, vfloat8e4m3m8_t hi, vfloat4e2m1m1_t a,
	 vfloat4e2m1m1_t b, vuint16m1_t s, size_t vl)
{
  vfloat8e4m3m16_t acc = __riscv_ime_vpair_f8e4m3m16 (lo, hi);
  acc = __riscv_vfwmmacc_scaled_vv_f8e4m3m16_f4e2m1m1_bs32 (acc, a, b, s, vl); /* { dg-error "requires 'SEW \\\* lambda' of at least 16" } */
  return __riscv_ime_vunpairlo_f8e4m3m16 (acc);
}
