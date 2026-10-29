/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zbb -mabi=lp64d -O2" } */

/* The runtime IME probe is hoisted to function entry, so it runs on
   paths that never reach the consumer and must not trap there.  The
   geometry check traps only on the path through the consumer: for
   i32m8 with _lm4 at VLEN=512, lambda^2 = 8 has no integer root, and
   main must still return 0 when called without arguments.  */

typedef __SIZE_TYPE__ size_t;
#pragma riscv intrinsic "vector"

static inline __attribute__ ((always_inline)) int
mma (void)
{
  vint32m8_t acc
    = __riscv_vqmmacc_vv_i32m8_lm4 (__riscv_vundefined_i32m8 (),
				    __riscv_vundefined_i8m4 (),
				    __riscv_vundefined_i8m4 (),
				    __riscv_vsetvlmax_e32m4 ());
  return __riscv_vmv_x_s_i32m8_i32 (acc);
}

int
f (int argc)
{
  if (argc < 2)
    return 0;
  return mma ();
}

/* Two consumers with the same geometry share one probe; each checks
   its result.  */
vint32m1_t
g (vint32m1_t acc, vint8m1_t a, vint8m1_t b, size_t vl, int c)
{
  if (c)
    acc = __riscv_vqmmacc_vv_i32m1 (acc, a, b, vl);
  else
    acc = __riscv_vqmmacc_vv_i32m1 (acc, b, a, vl);
  return acc;
}

/* No trap before the argc test.  */
/* { dg-final { scan-assembler-not {f:\n(?:(?!\tble|\tbgt|\tbge|\tblt)[^\n]*\n)*\tebreak} } } */
/* { dg-final { scan-assembler-times {\tcsrr\t[a-z0-9]+,vlenb} 2 } } */
/* { dg-final { scan-assembler-times {\tcsrr\t[a-z0-9]+,vtype} 2 } } */
