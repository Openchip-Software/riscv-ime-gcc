/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zbb_zvmma -mabi=lp64d -O2" } */

/* The whole-register moves of the m16 pair need a valid vtype before the
   runtime probe's IME vsetvl.  The vsetvl emitted for them takes over the
   IME setup of the probe, which is then redundant: only one IME vsetvl
   remains and no plain vsetvli is needed.  */

#pragma riscv intrinsic "vector"
typedef __SIZE_TYPE__ size_t;

vint64m8_t
f (vint64m8_t lo, vint64m8_t hi, vint8m8_t a1, vint8m8_t a2, size_t vl)
{
  return __riscv_ime_vunpairlo_i64m16 (
    __riscv_v8wmmacc_vv_i64m16_lm8 (__riscv_ime_vpair_i64m16 (lo, hi),
				    a1, a2, vl));
}

/* { dg-final { scan-assembler-times {\tvsetvl\t} 1 } } */
/* { dg-final { scan-assembler-not {\tvsetivli\t} } } */
/* { dg-final { scan-assembler-not {\tvsetvli\t} } } */
