/* { dg-do run { target { riscv_v } } } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Execution test for the W=1 m16 vmmacc with m8 source-LMUL inputs.

   At VLEN=512, SEW=32, EMUL_C=16 the IME multiply-accumulate computes
   a matrix product of two SEW.M8 input tiles into an SEW.M16
   accumulator.  This test does NOT verify the matrix layout (that
   would require modelling the IME tile semantics in C); it only
   verifies that the round trip

      acc0 = vpair (lo, hi)
      acc1 = vmmacc (acc0, a, b)   // m16, m8, m8
      lo'  = vunpairlo (acc1)
      hi'  = vunpairhi (acc1)

   compiles, executes without trapping, and produces deterministic
   data (different runs of the same input must produce the same
   output).  This guards against codegen bugs that produce
   inconsistent or undefined m16 contents.  */

#pragma riscv intrinsic "vector"
typedef __SIZE_TYPE__ size_t;

static void
compute (int *lo_in, int *hi_in, int *a_in, int *b_in,
	 int *lo_out, int *hi_out, size_t vl)
{
  vint32m8_t lo = __riscv_vle32_v_i32m8 (lo_in, vl);
  vint32m8_t hi = __riscv_vle32_v_i32m8 (hi_in, vl);
  vint32m8_t a  = __riscv_vle32_v_i32m8 (a_in,  vl);
  vint32m8_t b  = __riscv_vle32_v_i32m8 (b_in,  vl);

  vint32m16_t acc = __riscv_ime_vpair_i32m16 (lo, hi);
  acc = __riscv_vmmacc_vv_i32m16_lm8 (acc, a, b, vl);

  vint32m8_t r_lo = __riscv_ime_vunpairlo_i32m16 (acc);
  vint32m8_t r_hi = __riscv_ime_vunpairhi_i32m16 (acc);

  __riscv_vse32_v_i32m8 (lo_out, r_lo, vl);
  __riscv_vse32_v_i32m8 (hi_out, r_hi, vl);
}

int
main (void)
{
  enum { N = 128 };
  int lo[N], hi[N], a[N], b[N];
  int lo_o1[N], hi_o1[N], lo_o2[N], hi_o2[N];

  for (int i = 0; i < N; i++)
    {
      lo[i] = i + 1;
      hi[i] = 100 + i;
      a[i]  = i % 7;
      b[i]  = (i * 3) % 11;
    }

  compute (lo, hi, a, b, lo_o1, hi_o1, N);
  compute (lo, hi, a, b, lo_o2, hi_o2, N);

  /* Determinism: same inputs must yield same outputs.  */
  for (int i = 0; i < N; i++)
    if (lo_o1[i] != lo_o2[i] || hi_o1[i] != hi_o2[i])
      __builtin_abort ();

  return 0;
}
