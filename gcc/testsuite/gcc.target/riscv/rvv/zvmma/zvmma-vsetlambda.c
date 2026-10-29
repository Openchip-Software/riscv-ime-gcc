/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm -mabi=lp64d -O2" } */

/* Cover the compile-time forms of `__riscv_vsetlambda': each one
   rewrites the lambda field of vtype (csrr + RMW + `vsetvl x0,x0,t')
   and reads the result back (csrr + decode).  A requested lambda of 0
   writes the encoding 0, the preserve-or-initialize request; it is not
   a pure read.  An illegal constant is an error (verified by a separate
   dg-error test), and the runtime-argument form is tested
   separately.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

/* Preserve-or-initialize: also writes vtype.  */
size_t query (void)
{
  return __riscv_vsetlambda (0);
}

/* Each of these writes a distinct lambda encoding.  */
size_t set_1  (void) { return __riscv_vsetlambda (1);  }
size_t set_2  (void) { return __riscv_vsetlambda (2);  }
size_t set_4  (void) { return __riscv_vsetlambda (4);  }
size_t set_8  (void) { return __riscv_vsetlambda (8);  }
size_t set_16 (void) { return __riscv_vsetlambda (16); }
size_t set_32 (void) { return __riscv_vsetlambda (32); }
size_t set_64 (void) { return __riscv_vsetlambda (64); }

/* One `vsetvl x0,x0,...' per function, `query' included.  */
/* { dg-final { scan-assembler-times {vsetvl\tx0,x0,} 8 } } */

/* Each of the 8 functions reads vtype twice: for the RMW and for the
   readback.  */
/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vtype} 16 } } */
