/* { dg-do compile } */
/* { dg-options "-march=rv64gcv_zvmma_zvl4096b -mabi=lp64d -O2" } */

/* IME `Zvvmm' introduces LMUL=16 modes for the m16 accumulator.
   `poly_uint16' storage for `mode_size' / `mode_precision' caps the
   stored value at 65535, so VLEN * LMUL must stay below that.  With
   the base-RVV cap of MAX_LMUL=8, VLEN <= 4096 fits; with the m16
   LMUL=16 mode introduced by Zvvmm, the bound tightens to VLEN <=
   2048 -- raising it any higher overflows the stored size to 0 and
   silently produces wrong code in IRA + LRA.  riscv_override_options
   rejects this combination with a sorry; verify it fires.  */

int main (void) { return 0; }

/* { dg-message "sorry, unimplemented: IME .Zvvmm. with LMUL=16" "" { target *-*-* } 0 } */
/* { dg-prune-output "compilation terminated" } */
