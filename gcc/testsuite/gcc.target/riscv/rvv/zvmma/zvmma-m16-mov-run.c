/* { dg-do run { target { riscv_v } } } */
/* { dg-options "-march=rv64gcv_zvmma_zvl512b -mrvv-vector-bits=zvl -mabi=lp64d -O2" } */

/* Execution test for the m16 mov splitter's mem<->reg path.

   Force the compiler to spill an m16 value to the stack and reload
   it by hiding it across an asm barrier, then verify both halves
   come back intact.  Exercises the upper-half address arithmetic
   (`csrr vlenb; sll #3; add base') in the splitter.  RVV types
   cannot have static storage (no fixed size), so the spill must be
   compiler-induced rather than user-declared.  */

#pragma riscv intrinsic "vector"
typedef __SIZE_TYPE__ size_t;

int
main (void)
{
  enum { N = 128 }; /* m8 element count at VLEN=512, SEW=32.  */
  int lo_buf[N], hi_buf[N], r_lo_buf[N], r_hi_buf[N];

  for (int i = 0; i < N; i++)
    {
      lo_buf[i] = 0x30000 + i;
      hi_buf[i] = 0x40000 + i;
    }

  size_t vl = N;
  vint32m8_t lo = __riscv_vle32_v_i32m8 (lo_buf, vl);
  vint32m8_t hi = __riscv_vle32_v_i32m8 (hi_buf, vl);

  vint32m16_t acc = __riscv_ime_vpair_i32m16 (lo, hi);

  /* Force a spill of `acc' across this asm: it clobbers all vector
     registers, so the compiler must materialise `acc' to the stack
     before the asm and reload it after -- exercising the mov<m16>
     mem<->reg splitter path.  */
  asm volatile (""
		:
		:
		: "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
		  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
		  "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
		  "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31");

  vint32m8_t r_lo = __riscv_ime_vunpairlo_i32m16 (acc);
  vint32m8_t r_hi = __riscv_ime_vunpairhi_i32m16 (acc);

  __riscv_vse32_v_i32m8 (r_lo_buf, r_lo, vl);
  __riscv_vse32_v_i32m8 (r_hi_buf, r_hi, vl);

  for (int i = 0; i < N; i++)
    if (r_lo_buf[i] != lo_buf[i] || r_hi_buf[i] != hi_buf[i])
      __builtin_abort ();

  return 0;
}
