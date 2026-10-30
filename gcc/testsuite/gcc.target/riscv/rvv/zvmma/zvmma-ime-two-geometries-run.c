/* { dg-do run { target { riscv_v } } } */
/* { dg-options "-march=rv64gcv_zbb_zvmma -mabi=lp64d -O2" } */

/* Two multiply-accumulates with different geometries at the same SEW: an
   m4 accumulator in a loop and an m1 accumulator after it.  Both runtime
   probes run at function entry and the second leaves its lambda in vtype,
   so the multiply-accumulate in the loop needs its own IME vsetvl.  With
   all-ones inputs and zero accumulators, every element of a result is the
   lambda of its geometry.  */

#pragma riscv intrinsic "vector"
typedef __SIZE_TYPE__ size_t;

int A[64] = {[0 ... 63] = 1};
int C[1024], D[1024];

/* Return lambda for an SEW=32 accumulator of EMUL_C registers, or 0 if
   VLEN / (32 * EMUL_C) is not the square of a power of two.  */
static int
lambda_for (unsigned vlen, unsigned emul_c)
{
  unsigned sq = vlen / (32 * emul_c);
  for (int l = 1; l <= 64; l *= 2)
    if ((unsigned) (l * l) == sq)
      return l;
  return 0;
}

int __attribute__ ((noipa))
run (int n)
{
  size_t vl = __riscv_vsetvlmax_e32m1 ();

  vint32m4_t c = __riscv_vmv_v_x_i32m4 (0, __riscv_vsetvlmax_e32m4 ());
  for (int k = 0; k < n; k++)
    {
      vint32m1_t a = __riscv_vle32_v_i32m1 (A + (k & 1), vl);
      c = __riscv_vmmacc_vv_i32m4 (c, a, a, vl);
    }
  __riscv_vse32_v_i32m4 (C, c, __riscv_vsetvlmax_e32m4 ());

  vint32m1_t a = __riscv_vle32_v_i32m1 (A, vl);
  vint32m1_t d = __riscv_vmv_v_x_i32m1 (0, vl);
  d = __riscv_vmmacc_vv_i32m1 (d, a, a, vl);
  __riscv_vse32_v_i32m1 (D, d, vl);
  return 0;
}

int
main (void)
{
  unsigned vlen = __riscv_vsetvlmax_e32m1 () * 32;
  int l4 = lambda_for (vlen, 4), l1 = lambda_for (vlen, 1);
  if (!l4 || !l1 || vlen * 4 / 32 > 1024)
    return 0;

  run (1);

  for (unsigned i = 0; i < vlen * 4 / 32; i++)
    if (C[i] != l4)
      __builtin_abort ();
  for (unsigned i = 0; i < vlen / 32; i++)
    if (D[i] != l1)
      __builtin_abort ();
  return 0;
}
