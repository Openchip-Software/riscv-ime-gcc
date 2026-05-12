/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvvmm -mabi=lp64d -O2" } */

/* Cover the three compile-time forms of `__riscv_vsetlambda':

     - requested_lambda == 0   pure read-only: csrr + extract + decode,
                               no vsetvl.
     - requested_lambda == N   (N in {1,2,4,8,16,32,64}) write-half:
                               csrr + RMW + `vsetvl x0,x0,t' + readback
                               + decode.
     - illegal const N         compile-time error (verified by a
                               separate dg-error test).

   Runtime-arg form is intentionally not exercised here -- it currently
   issues a sorry().  A later commit will lower it to a branch on
   arg == 0.  */

typedef unsigned long size_t;
#pragma riscv intrinsic "vector"

/* Read-only: no write to vtype.  */
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

/* One `vsetvl x0,x0,...' per non-zero call -- 7 functions.  The
   read-only `query' must NOT emit a vsetvl.  */
/* { dg-final { scan-assembler-times {vsetvl\tx0,x0,} 7 } } */

/* Each of the 8 functions (query + 7 setters) reads vtype at the end
   for the return-value readback; the setters also csrr vtype at the
   start of the RMW.  Lower bound: 8 functions ≥ 8 csrr.  */
/* { dg-final { scan-assembler-times {csrr\t[a-z0-9]+,vtype} 15 } } */
