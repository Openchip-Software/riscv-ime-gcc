;; Machine description for RISC-V IME (Integrated Matrix Extension)
;; Copyright (C) 2026 Free Software Foundation, Inc.

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful, but
;; WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;; General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;; UNSPECs of the IME instructions.

(define_c_enum "unspec" [
  UNSPEC_VMMACC_S    ;; Zvvmm vmmacc.vv,  signed*signed    (altfmt_A=0, altfmt_B=0)
  UNSPEC_VMMACC_U    ;; Zvvmm vmmacc.vv,  unsigned*unsigned (altfmt_A=1, altfmt_B=1)
  UNSPEC_VMMACC_SU   ;; Zvvmm vmmacc.vv,  signed*unsigned   (altfmt_A=0, altfmt_B=1)
  UNSPEC_VMMACC_US   ;; Zvvmm vmmacc.vv,  unsigned*signed   (altfmt_A=1, altfmt_B=0)
  UNSPEC_VWMMACC_S   ;; Zvvmm vwmmacc.vv, signed*signed    (W=2, altfmt_A=0, altfmt_B=0)
  UNSPEC_VWMMACC_U   ;; Zvvmm vwmmacc.vv, unsigned*unsigned (W=2, altfmt_A=1, altfmt_B=1)
  UNSPEC_VWMMACC_SU  ;; Zvvmm vwmmacc.vv, signed*unsigned  (W=2, altfmt_A=0, altfmt_B=1)
  UNSPEC_VWMMACC_US  ;; Zvvmm vwmmacc.vv, unsigned*signed  (W=2, altfmt_A=1, altfmt_B=0)
  UNSPEC_VQWMMACC_S  ;; Zvvmm vqwmmacc.vv, signed*signed   (W=4, altfmt_A=0, altfmt_B=0)
  UNSPEC_VQWMMACC_U  ;; Zvvmm vqwmmacc.vv, unsigned*unsigned(W=4, altfmt_A=1, altfmt_B=1)
  UNSPEC_VQWMMACC_SU ;; Zvvmm vqwmmacc.vv, signed*unsigned (W=4, altfmt_A=0, altfmt_B=1)
  UNSPEC_VQWMMACC_US ;; Zvvmm vqwmmacc.vv, unsigned*signed (W=4, altfmt_A=1, altfmt_B=0)
  UNSPEC_V8WMMACC_S  ;; Zvvmm v8wmmacc.vv, signed*signed   (W=8, altfmt_A=0, altfmt_B=0)
  UNSPEC_V8WMMACC_U  ;; Zvvmm v8wmmacc.vv, unsigned*unsigned(W=8, altfmt_A=1, altfmt_B=1)
  UNSPEC_V8WMMACC_SU ;; Zvvmm v8wmmacc.vv, signed*unsigned (W=8, altfmt_A=0, altfmt_B=1)
  UNSPEC_V8WMMACC_US ;; Zvvmm v8wmmacc.vv, unsigned*signed (W=8, altfmt_A=1, altfmt_B=0)

  UNSPEC_VMTL        ;; Zvvmtls  order-preserving tile load  (vmtl.v)
  UNSPEC_VMTS        ;; Zvvmtls  order-preserving tile store (vmts.v)
  UNSPEC_VMTTL       ;; Zvvmttls transposing tile load       (vmttl.v)
  UNSPEC_VMTTS       ;; Zvvmttls transposing tile store      (vmtts.v)

  ;; Zvvfmm - FP matrix multiply-accumulate
  UNSPEC_VFMMACC			;; vfmmacc.vv (W=1, same-format FP)
  ;; W=1 vfmmacc OFP8 -> OFP8: 8 UNSPECs covering all (acc,vs1,vs2)
  ;; altfmt combinations.  Naming: F8_<acc>_<vs1>_<vs2>.
  UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3
  UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2
  UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3
  UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2
  UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3
  UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2
  UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3
  UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2
  ;; W=2 vfwmmacc OFP8 -> fp16 / bf16: the 4 input combinations for
  ;; each accumulator format.
  UNSPEC_VFWMMACC_OFP8_FP16_E4M3
  UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2
  UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3
  UNSPEC_VFWMMACC_OFP8_FP16_E5M2
  UNSPEC_VFWMMACC_OFP8_BF16_E4M3
  UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2
  UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3
  UNSPEC_VFWMMACC_OFP8_BF16_E5M2
  UNSPEC_VFWMMACC_F16			;; vfwmmacc.vv (W=2, FP16->FP32)
  UNSPEC_VFWMMACC_BF16			;; vfwmmacc.vv (W=2, BF16->FP32)
  UNSPEC_VFWMMACC_F32			;; vfwmmacc.vv (W=2, FP32->FP64)
  UNSPEC_VFQWMMACC_F16			;; vfqwmmacc.vv (W=4, FP16->FP64)
  UNSPEC_VFQWMMACC_BF16		;; vfqwmmacc.vv (W=4, BF16->FP64)
  UNSPEC_VFQWMMACC_F8E4M3		;; vfqwmmacc.vv (W=4, vs1=E4M3, vs2=E4M3 -> FP32)
  UNSPEC_VFQWMMACC_F8E5M2		;; vfqwmmacc.vv (W=4, vs1=E5M2, vs2=E5M2 -> FP32)
  UNSPEC_VFQWMMACC_F8E4M3_E5M2	;; vfqwmmacc.vv (W=4, vs1=E4M3, vs2=E5M2 -> FP32)
  UNSPEC_VFQWMMACC_F8E5M2_E4M3	;; vfqwmmacc.vv (W=4, vs1=E5M2, vs2=E4M3 -> FP32)
  UNSPEC_VF8WMMACC_F8E4M3		;; vf8wmmacc.vv (W=8, vs1=E4M3, vs2=E4M3 -> FP64)
  UNSPEC_VF8WMMACC_F8E5M2		;; vf8wmmacc.vv (W=8, vs1=E5M2, vs2=E5M2 -> FP64)
  UNSPEC_VF8WMMACC_F8E4M3_E5M2	;; vf8wmmacc.vv (W=8, vs1=E4M3, vs2=E5M2 -> FP64)
  UNSPEC_VF8WMMACC_F8E5M2_E4M3	;; vf8wmmacc.vv (W=8, vs1=E5M2, vs2=E4M3 -> FP64)
  UNSPEC_VF8WMMACC_F4E2M1		;; vf8wmmacc.vv (W=8, OFP4_E2M1->FP32)

  ;; Microscaled FP-input variants: the mnemonics of the unscaled ones,
  ;; with the block scales in v0 and the bs vtype bit set.
  UNSPEC_VFWMMACC_F16_BS32	;; vfwmmacc.vv scaled, FP16->FP32, BS=32
  UNSPEC_VFWMMACC_F16_BS16	;; vfwmmacc.vv scaled, FP16->FP32, BS=16
  UNSPEC_VFQWMMACC_F8E4M3_BS32	;; vfqwmmacc.vv scaled, OFP8_E4M3->FP32, BS=32
  UNSPEC_VFQWMMACC_F8E4M3_BS16	;; vfqwmmacc.vv scaled, OFP8_E4M3->FP32, BS=16
  UNSPEC_VF8WMMACC_F8E4M3_BS32	;; vf8wmmacc.vv scaled, OFP8_E4M3->FP64, BS=32
  UNSPEC_VF8WMMACC_F8E4M3_BS16	;; vf8wmmacc.vv scaled, OFP8_E4M3->FP64, BS=16

  ;; Microscaled integer-input variants.
  UNSPEC_VFWIMMACC_I8_BS32	;; vfwimmacc.vv, Int8->FP16, BS=32
  UNSPEC_VFWIMMACC_I8_BS16	;; vfwimmacc.vv, Int8->FP16, BS=16
  UNSPEC_VFQWIMMACC_I8_BS32	;; vfqwimmacc.vv, Int8->FP32, BS=32
  UNSPEC_VFQWIMMACC_I8_BS16	;; vfqwimmacc.vv, Int8->FP32, BS=16
  UNSPEC_VF8WIMMACC_I8_BS32	;; vf8wimmacc.vv, Int8->FP64, BS=32
  UNSPEC_VF8WIMMACC_I8_BS16	;; vf8wimmacc.vv, Int8->FP64, BS=16

  ;; Register-form vsetvl used to establish the full vtype value for IME
  ;; instructions (lambda, altfmt_A, altfmt_B, altfmt, bs in addition to the
  ;; standard SEW/LMUL/ta/ma).  A plain vsetvli/vsetivli cannot encode these
  ;; upper vtype bits; they require a register source containing the full
  ;; vtype value.
  UNSPEC_VSETVL_IME

  ;; IME `pair' / `unpair' primitives.  These map an m16 accumulator
  ;; value (occupying 16 consecutive vector registers at a 16-aligned
  ;; base, v0..v15 or v16..v31) to/from its two m8 halves (8 regs
  ;; each).  No vector instruction is required when the m8 source(s)
  ;; or destination are already at the right offset within the m16
  ;; register group; otherwise the post-RA splitter emits the
  ;; necessary `vmv8r.v' move(s).
  UNSPEC_VPAIR        ;; (vpair m8_lo m8_hi)        -> m16
  UNSPEC_VUNPAIRLO    ;; (vunpairlo m16)            -> m8  (lower half)
  UNSPEC_VUNPAIRHI    ;; (vunpairhi m16)            -> m8  (upper half)
])

(define_c_enum "unspecv" [
  ;; User-controlled write of vtype.lambda only.  Implements the
  ;; `__riscv_vsetlambda(N)' C intrinsic write-half: reads vtype, RMWs
  ;; the lambda field with the encoded value N, executes
  ;; `vsetvl x0, x0, t1' to install the new vtype while preserving vl,
  ;; and leaves all other vtype fields unchanged (subject to WARL
  ;; clamping by hardware).  See the spec section "Establishing lambda
  ;; from C: __riscv_vsetlambda" for the WARL contract.  The vsetvl
  ;; pass treats this as a hard DEF of vtype.lambda when operand 0 is
  ;; a const_int in the supported set, and as a may-DEF otherwise.
  UNSPECV_VSETLAMBDA

  ;; Volatile-read of the hardware vtype CSR.  Used inside the
  ;; vsetlambda RMW (csrr -> mask -> OR -> install) and inside the
  ;; runtime-VLEN WARL verification sequence.  Must remain ordered
  ;; with respect to surrounding vtype writes; the pre-vsetvl-pass
  ;; placeholder `vsetvl_no_side_effects' does not DEF VTYPE_REGNUM
  ;; in dataflow terms, so a plain `unspec' read could be hoisted
  ;; above it and observe stale vtype after the pass materialises a
  ;; real vsetvli at the placeholder's site.
  UNSPECV_READ_VTYPE

  ;; Lambda-preserving vsetvl: read current vtype.lambda dynamically
  ;; and merge it with a compile-time-known (SEW, LMUL, ta, ma,
  ;; altfmt, bs) bit pattern, then issue a register-form vsetvl.
  ;; Emitted by the vsetvl pass before an IME tile/mmacc consumer
  ;; whose lambda reach-state in the lambda lattice is
  ;; DEFINED_RUNTIME (a `__riscv_vsetlambda(v)' with a runtime arg
  ;; established lambda earlier; the value is unknown statically but
  ;; must be preserved across the implicit vtype reconfiguration
  ;; this insn performs).  See the corresponding emission branch in
  ;; `pre_vsetvl::insert_vsetvl_insn'.
  UNSPECV_VSETVL_PRESERVE_LAMBDA
])

;; -----------------------------------------------------------------------
;; ---- IME `m16' move expander
;; -----------------------------------------------------------------------
;;
;; The IME spec confines RVVM16* (W=1 accumulator) values to mmacc,
;; pair, and unpair operations.  However the register allocator and
;; init-regs pass need a `mov<mode>' for any mode that can live across
;; a pseudo boundary: an inline asm that produces an m16, a spill of an
;; mmacc result, a CONST0 init.  The standard mov<mode> expanders in
;; vector.md are gated on V / V_WHOLE / V_FRACT, none of which include
;; the m16 modes -- so without this pattern emit_move_insn falls back
;; to emit_move_multi_word, which ICEs on VLA modes.
;;
;; The strategy keeps m16 isolated from the generic RVV pattern surface
;; (as the spec requires): we accept (set m16 m16) at pseudo level and
;; split it post-RA into two m8 hard-register transfers, using the
;; m16-alignment guarantee that riscv_hard_regno_mode_ok enforces
;; (v0..v15 or v16..v31).  At that point the lower half occupies the
;; first 8 vector registers of the group and the upper half the next 8,
;; so the split is a straightforward (regno, regno+8) decomposition.
;; The standard `mov<mode>' expand carries no Pmode iterator -- if it
;; did, gentarget-def would see the cross product (VM16 x P) and reject
;; the resulting two `movrvvm16qi' definitions as duplicates (the
;; Pmode==SImode / Pmode==DImode C-conditions disambiguate at runtime
;; but not at codegen).  Instead the expand emits the scratch-bearing
;; PARALLEL by hand, with the scratch typed in Pmode so the splitter's
;; address arithmetic matches on both RV32 and RV64.
(define_expand "mov<mode>"
  [(set (match_operand:VM16 0 "nonimmediate_operand")
	(match_operand:VM16 1 "general_operand"))]
  "TARGET_ZVVM_MM"
{
  /* Accept the all-zero CONST_VECTOR (init-regs uses it to zero an
     m16 pseudo that is read before being defined); reject every
     other immediate.  */
  if (CONST_VECTOR_P (operands[1])
      && operands[1] != CONST0_RTX (<MODE>mode))
    FAIL;
  /* mem-to-mem: route through a fresh m16 pseudo so each half is a
     simple mem<->reg in the post-RA splitter.  */
  if (MEM_P (operands[0]) && MEM_P (operands[1]))
    operands[1] = force_reg (<MODE>mode, operands[1]);

  emit_insn (gen_rtx_PARALLEL (VOIDmode,
    gen_rtvec (2,
      gen_rtx_SET (operands[0], operands[1]),
      gen_rtx_CLOBBER (VOIDmode, gen_rtx_SCRATCH (Pmode)))));
  DONE;
})

;; The scratch tracks Pmode via the `P' iterator (defined in
;; iterators.md as the build's active Pmode -- SI for ILP32, DI for
;; LP64), so the upper-half address arithmetic in the splitter stays
;; correct on both targets.  The pattern names and inner mode
;; references use the explicit `<VM16:...>' prefix because both the
;; VM16 and P iterators contribute a `mode' attribute -- an unqualified
;; `<mode>' would be ambiguous and would fail at gentarget-def time.
(define_insn_and_split "*mov<VM16:mode>_m16"
  [(set (match_operand:VM16 0 "nonimmediate_operand" "=vr,vr, vr,  m")
	(match_operand:VM16 1 "vm16_src_operand"     " vr, m,Wc0, vr"))
   (clobber (match_scratch:P 2 "=&r,&r,&r,&r"))]
  "TARGET_ZVVM_MM
   && (register_operand (operands[0], <VM16:MODE>mode)
       || register_operand (operands[1], <VM16:MODE>mode)
       || operands[1] == CONST0_RTX (<VM16:MODE>mode))"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  machine_mode hmode = riscv_vector::m16_half_mode_md (<VM16:MODE>mode);
  poly_int64 hbytes = GET_MODE_SIZE (hmode);
  rtx dst = operands[0], src = operands[1], scratch = operands[2];

  /* For a register operand the upper half lives at regno + 8 (m8
     occupies 8 vector registers, m16 occupies 16, the m16 group is
     16-aligned by HARD_REGNO_MODE_OK).  For a CONST0 source both
     halves use the m8 all-zero CONST_VECTOR.

     For a memory operand the upper half lives at base + hbytes.
     RVV whole-vector loads/stores accept only a register-indirect
     address (no displacement), so the upper-half base has to be
     materialised in a scratch GR register first.  The expander
     forces mem-to-mem through a fresh m16 pseudo, so at most ONE of
     {dst, src} reaches the splitter as a MEM -- the scratch is
     reused only for that side's upper-half address.

     SCRATCH first holds `hbytes' (i.e. `8 * vlenb' for the m8 half
     of an m16 group), computed once via `vlenb << 3'; that value
     is added in place to the upper-half base.  All address
     arithmetic happens in Pmode so the splitter is correct for
     both ILP32 and LP64.  */

  /* Stage 1: SCRATCH = hbytes (= 8 * BYTES_PER_RISCV_VECTOR).  Done
     unconditionally even if only zero-offset half_mem calls follow
     -- the clobber is benign and the value lets later reuse skip
     the recomputation.  Stage 2 (the add to base) happens lazily
     inside half_mem the first time a non-zero offset is requested,
     and only once because the same memory operand's lower half is
     handled separately with a zero offset.  */
  bool scratch_holds_offset = false;
  bool scratch_holds_addr = false;
  (void) scratch_holds_addr;

  auto ensure_offset_in_scratch = [&] ()
    {
      if (scratch_holds_offset)
	return;
      /* Load BYTES_PER_RISCV_VECTOR (a poly_int) into SCRATCH via the
	 standard `csrr vlenb' move pattern, then shift-left by
	 log2 (LMUL) so the value held is hbytes (= LMUL * vlenb).
	 Derive the shift count from `hmode' rather than hard-coding
	 it: any future widening of m8 or change in the half-mode
	 mapping for VM16 stays consistent with the address
	 arithmetic.  */
      int lmul = exact_div (GET_MODE_SIZE (hmode), BYTES_PER_RISCV_VECTOR)
		   .to_constant ();
      gcc_assert (lmul >= 2 && pow2p_hwi (lmul));
      int shift = exact_log2 (lmul);
      emit_move_insn (scratch, gen_int_mode (BYTES_PER_RISCV_VECTOR, Pmode));
      emit_insn (gen_rtx_SET (scratch,
			      gen_rtx_ASHIFT (Pmode, scratch,
					      gen_int_mode (shift, Pmode))));
      scratch_holds_offset = true;
    };

  auto half_mem = [&] (rtx mem, poly_int64 offset) -> rtx
    {
      if (known_eq (offset, 0))
	return adjust_address (mem, hmode, 0);
      /* The only non-zero offset the splitter ever passes is hbytes
	 (m8 half size).  Guard against a future code path that ever
	 passes a different offset and against being called twice with
	 non-zero offset -- the second call would overwrite the
	 scratch the first call's MEM still references, silently
	 generating a store/load to the wrong address.  */
      gcc_assert (known_eq (offset, hbytes));
      gcc_assert (!scratch_holds_addr);
      ensure_offset_in_scratch ();
      rtx base = XEXP (mem, 0);
      emit_insn (gen_rtx_SET (scratch,
			      gen_rtx_PLUS (Pmode, scratch, base)));
      scratch_holds_addr = true;
      return replace_equiv_address (adjust_address (mem, hmode, 0), scratch);
    };

  rtx d_lo, d_hi, s_lo, s_hi;

  if (REG_P (dst))
    {
      unsigned r = REGNO (dst);
      d_lo = gen_rtx_REG (hmode, r);
      d_hi = gen_rtx_REG (hmode, r + 8);
    }
  else
    {
      d_lo = half_mem (dst, 0);
      d_hi = half_mem (dst, hbytes);
    }

  if (src == CONST0_RTX (<VM16:MODE>mode))
    {
      s_lo = CONST0_RTX (hmode);
      s_hi = CONST0_RTX (hmode);
    }
  else if (REG_P (src))
    {
      unsigned r = REGNO (src);
      s_lo = gen_rtx_REG (hmode, r);
      s_hi = gen_rtx_REG (hmode, r + 8);
    }
  else
    {
      s_lo = half_mem (src, 0);
      s_hi = half_mem (src, hbytes);
    }

  emit_move_insn (d_lo, s_lo);
  emit_move_insn (d_hi, s_hi);
  DONE;
}
  [(set_attr "type" "vmov")
   (set_attr "mode" "<VM16:MODE>")])

;; Signedness of the two integer inputs of IME multiply-accumulate
;; instructions, as the vtype altfmt_A / altfmt_B bits that the vsetvl
;; pass sets.
(define_attr "ime_altfmt" "ss,uu,su,us"
  (const_string "ss"))

;; FP-only altfmt encoding for IME mmacc patterns whose operand modes
;; cannot disambiguate the input/accumulator format.  Specifically: OFP8
;; E4M3 vs E5M2 at SEW=8 share RVVM*QI mode and need explicit
;; per-pattern altfmt selection.
;;
;; The 8 enumerators are <A>_<B>_<F>, where each token is e4m3 (altfmt
;; bit 0) or e5m2 (altfmt bit 1):
;;   A = altfmt_A (vs1 input format)
;;   B = altfmt_B (vs2 input format)
;;   F = altfmt   (accumulator/output format -- only meaningful at W=1
;;                 with OFP8 acc, and at W=2 where the SEW=16 acc
;;                 selects fp16 (0) vs bf16 (1))
;;
;; Default "none" means "do not override" -- riscv_extract_ime_-
;; requirements falls back to the BFmode-of-operand inference and the
;; integer-altfmt logic.  The override path triggers only when a
;; pattern explicitly sets a non-"none" value.
(define_attr "ime_fp_altfmt"
  "none,
   e4m3_e4m3_e4m3, e4m3_e4m3_e5m2,
   e4m3_e5m2_e4m3, e4m3_e5m2_e5m2,
   e5m2_e4m3_e4m3, e5m2_e4m3_e5m2,
   e5m2_e5m2_e4m3, e5m2_e5m2_e5m2"
  (const_string "none"))

;; Whether the instruction is an IME tile load or store.  These read
;; vtype.lambda but have the same type (vlds/vsts) as the ordinary
;; strided loads and stores.
(define_attr "ime_tile" "no,yes"
  (const_string "no"))

;; Block scale size for microscaled variants.
;; "none" means not a microscaled instruction (bs bit irrelevant).
;; "bs16" and "bs32" set the bs vtype bit accordingly.
(define_attr "ime_bs" "none,bs16,bs32"
  (const_string "none"))

;; Operand index of the hidden "vtype source" operand on IME consumer
;; patterns.  Default INVALID_ATTRIBUTE (no such operand).  Otherwise
;; this is the index of a `(use (match_operand N "vtype_op_operand"))'
;; clause.  The operand carries no MD-level mode (the special predicate
;; accepts only a Pmode register or const0_rtx).  The runtime-VLEN IME
;; path threads the full vtype rtx through this operand so the vsetvl
;; pass can re-anchor IME vtype right before the consumer after
;; spill-induced clobbers.  Static-VLEN consumers carry `const0_rtx'
;; as a placeholder (the pass synthesises its own const vtype via
;; `vsetvl_ime' for the static path).
(define_attr "ime_vtype_op_idx" ""
  (const_int INVALID_ATTRIBUTE))

;; The IME integer matrix multiply-accumulates: one UNSPEC for each
;; instruction and signedness of the A and B inputs.
(define_int_iterator IME_INT_MMACC
  [UNSPEC_VMMACC_S   UNSPEC_VMMACC_U   UNSPEC_VMMACC_SU   UNSPEC_VMMACC_US
   UNSPEC_VWMMACC_S  UNSPEC_VWMMACC_U  UNSPEC_VWMMACC_SU  UNSPEC_VWMMACC_US
   UNSPEC_VQWMMACC_S UNSPEC_VQWMMACC_U UNSPEC_VQWMMACC_SU UNSPEC_VQWMMACC_US
   UNSPEC_V8WMMACC_S UNSPEC_V8WMMACC_U UNSPEC_V8WMMACC_SU UNSPEC_V8WMMACC_US])

;; The same-format vfmmacc (FP16, BF16, FP32 and FP64).
(define_int_iterator IME_VFMMACC [UNSPEC_VFMMACC])

;; The other IME FP matrix multiply-accumulates: one UNSPEC for each
;; combination of accumulator and input formats.
(define_int_iterator IME_FP_MMACC
  [UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2
   UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2
   UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2
   UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2
   UNSPEC_VFWMMACC_F16 UNSPEC_VFWMMACC_BF16 UNSPEC_VFWMMACC_F32
   UNSPEC_VFWMMACC_OFP8_FP16_E4M3 UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2
   UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 UNSPEC_VFWMMACC_OFP8_FP16_E5M2
   UNSPEC_VFWMMACC_OFP8_BF16_E4M3 UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2
   UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 UNSPEC_VFWMMACC_OFP8_BF16_E5M2
   UNSPEC_VFQWMMACC_F16 UNSPEC_VFQWMMACC_BF16
   UNSPEC_VFQWMMACC_F8E4M3 UNSPEC_VFQWMMACC_F8E5M2
   UNSPEC_VFQWMMACC_F8E4M3_E5M2 UNSPEC_VFQWMMACC_F8E5M2_E4M3
   UNSPEC_VF8WMMACC_F8E4M3 UNSPEC_VF8WMMACC_F8E5M2
   UNSPEC_VF8WMMACC_F8E4M3_E5M2 UNSPEC_VF8WMMACC_F8E5M2_E4M3])

;; The microscaled FP multiply-accumulates: one UNSPEC for each
;; instruction, input format and block size.
(define_int_iterator IME_SCALED_MMACC
  [UNSPEC_VFWMMACC_F16_BS32 UNSPEC_VFWMMACC_F16_BS16
   UNSPEC_VFQWMMACC_F8E4M3_BS32 UNSPEC_VFQWMMACC_F8E4M3_BS16
   UNSPEC_VF8WMMACC_F8E4M3_BS32 UNSPEC_VF8WMMACC_F8E4M3_BS16
   UNSPEC_VFWIMMACC_I8_BS32 UNSPEC_VFWIMMACC_I8_BS16
   UNSPEC_VFQWIMMACC_I8_BS32 UNSPEC_VFQWIMMACC_I8_BS16
   UNSPEC_VF8WIMMACC_I8_BS32 UNSPEC_VF8WIMMACC_I8_BS16])

(define_int_attr ime_scaled_bs
  [(UNSPEC_VFWMMACC_F16_BS32 "32") (UNSPEC_VFWMMACC_F16_BS16 "16")
   (UNSPEC_VFQWMMACC_F8E4M3_BS32 "32") (UNSPEC_VFQWMMACC_F8E4M3_BS16 "16")
   (UNSPEC_VF8WMMACC_F8E4M3_BS32 "32") (UNSPEC_VF8WMMACC_F8E4M3_BS16 "16")
   (UNSPEC_VFWIMMACC_I8_BS32 "32") (UNSPEC_VFWIMMACC_I8_BS16 "16")
   (UNSPEC_VFQWIMMACC_I8_BS32 "32") (UNSPEC_VFQWIMMACC_I8_BS16 "16")
   (UNSPEC_VF8WIMMACC_I8_BS32 "32") (UNSPEC_VF8WIMMACC_I8_BS16 "16")])

;; The input format of each FP multiply-accumulate, as riscv_zvvm_format
;; names it.
(define_int_attr ime_fp_in
  [(UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 "OFP8") (UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2 "OFP8")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 "OFP8") (UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2 "OFP8")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 "OFP8") (UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2 "OFP8")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 "OFP8") (UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2 "OFP8")
   (UNSPEC_VFWMMACC_F16 "FP16") (UNSPEC_VFWMMACC_BF16 "BF16")
   (UNSPEC_VFWMMACC_F32 "FP32")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3 "OFP8") (UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2 "OFP8")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 "OFP8") (UNSPEC_VFWMMACC_OFP8_FP16_E5M2 "OFP8")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3 "OFP8") (UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2 "OFP8")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 "OFP8") (UNSPEC_VFWMMACC_OFP8_BF16_E5M2 "OFP8")
   (UNSPEC_VFQWMMACC_F16 "FP16") (UNSPEC_VFQWMMACC_BF16 "BF16")
   (UNSPEC_VFQWMMACC_F8E4M3 "OFP8") (UNSPEC_VFQWMMACC_F8E5M2 "OFP8")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "OFP8") (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "OFP8")
   (UNSPEC_VF8WMMACC_F8E4M3 "OFP8") (UNSPEC_VF8WMMACC_F8E5M2 "OFP8")
   (UNSPEC_VF8WMMACC_F8E4M3_E5M2 "OFP8") (UNSPEC_VF8WMMACC_F8E5M2_E4M3 "OFP8")
   (UNSPEC_VFWMMACC_F16_BS32 "FP16") (UNSPEC_VFWMMACC_F16_BS16 "FP16")
   (UNSPEC_VFQWMMACC_F8E4M3_BS32 "MXFP8") (UNSPEC_VFQWMMACC_F8E4M3_BS16 "MXFP8")
   (UNSPEC_VF8WMMACC_F8E4M3_BS32 "MXFP8") (UNSPEC_VF8WMMACC_F8E4M3_BS16 "MXFP8")
   (UNSPEC_VFWIMMACC_I8_BS32 "MXINT8") (UNSPEC_VFWIMMACC_I8_BS16 "MXINT8")
   (UNSPEC_VFQWIMMACC_I8_BS32 "MXINT8") (UNSPEC_VFQWIMMACC_I8_BS16 "MXINT8")
   (UNSPEC_VF8WIMMACC_I8_BS32 "MXINT8") (UNSPEC_VF8WIMMACC_I8_BS16 "MXINT8")])

;; The pattern name and the instruction of each multiply-accumulate.
(define_int_attr ime_mmacc
  [(UNSPEC_VMMACC_S "vmmacc_s") (UNSPEC_VMMACC_U "vmmacc_u")
   (UNSPEC_VMMACC_SU "vmmacc_su") (UNSPEC_VMMACC_US "vmmacc_us")
   (UNSPEC_VWMMACC_S "vwmmacc_s") (UNSPEC_VWMMACC_U "vwmmacc_u")
   (UNSPEC_VWMMACC_SU "vwmmacc_su") (UNSPEC_VWMMACC_US "vwmmacc_us")
   (UNSPEC_VQWMMACC_S "vqwmmacc_s") (UNSPEC_VQWMMACC_U "vqwmmacc_u")
   (UNSPEC_VQWMMACC_SU "vqwmmacc_su") (UNSPEC_VQWMMACC_US "vqwmmacc_us")
   (UNSPEC_V8WMMACC_S "v8wmmacc_s") (UNSPEC_V8WMMACC_U "v8wmmacc_u")
   (UNSPEC_V8WMMACC_SU "v8wmmacc_su") (UNSPEC_V8WMMACC_US "v8wmmacc_us")
   (UNSPEC_VFMMACC "vfmmacc")
   (UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 "vfmmacc_f8_e4m3_e4m3_e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2 "vfmmacc_f8_e4m3_e4m3_e5m2")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 "vfmmacc_f8_e4m3_e5m2_e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2 "vfmmacc_f8_e4m3_e5m2_e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 "vfmmacc_f8_e5m2_e4m3_e4m3")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2 "vfmmacc_f8_e5m2_e4m3_e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 "vfmmacc_f8_e5m2_e5m2_e4m3")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2 "vfmmacc_f8_e5m2_e5m2_e5m2")
   (UNSPEC_VFWMMACC_F16 "vfwmmacc_f16") (UNSPEC_VFWMMACC_BF16 "vfwmmacc_bf16")
   (UNSPEC_VFWMMACC_F32 "vfwmmacc_f32")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3 "vfwmmacc_ofp8_fp16_e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2 "vfwmmacc_ofp8_fp16_e4m3_e5m2")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 "vfwmmacc_ofp8_fp16_e5m2_e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2 "vfwmmacc_ofp8_fp16_e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3 "vfwmmacc_ofp8_bf16_e4m3")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2 "vfwmmacc_ofp8_bf16_e4m3_e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 "vfwmmacc_ofp8_bf16_e5m2_e4m3")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2 "vfwmmacc_ofp8_bf16_e5m2")
   (UNSPEC_VFQWMMACC_F16 "vfqwmmacc_f16")
   (UNSPEC_VFQWMMACC_BF16 "vfqwmmacc_bf16")
   (UNSPEC_VFQWMMACC_F8E4M3 "vfqwmmacc_f8e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2 "vfqwmmacc_f8e5m2")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "vfqwmmacc_f8e4m3_e5m2")
   (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "vfqwmmacc_f8e5m2_e4m3")
   (UNSPEC_VF8WMMACC_F8E4M3 "vf8wmmacc_f8e4m3")
   (UNSPEC_VF8WMMACC_F8E5M2 "vf8wmmacc_f8e5m2")
   (UNSPEC_VF8WMMACC_F8E4M3_E5M2 "vf8wmmacc_f8e4m3_e5m2")
   (UNSPEC_VF8WMMACC_F8E5M2_E4M3 "vf8wmmacc_f8e5m2_e4m3")
   (UNSPEC_VFWMMACC_F16_BS32 "vfwmmacc_scaled_f16_bs32")
   (UNSPEC_VFWMMACC_F16_BS16 "vfwmmacc_scaled_f16_bs16")
   (UNSPEC_VFQWMMACC_F8E4M3_BS32 "vfqwmmacc_scaled_f8e4m3_bs32")
   (UNSPEC_VFQWMMACC_F8E4M3_BS16 "vfqwmmacc_scaled_f8e4m3_bs16")
   (UNSPEC_VF8WMMACC_F8E4M3_BS32 "vf8wmmacc_scaled_f8e4m3_bs32")
   (UNSPEC_VF8WMMACC_F8E4M3_BS16 "vf8wmmacc_scaled_f8e4m3_bs16")
   (UNSPEC_VFWIMMACC_I8_BS32 "vfwimmacc_i8_bs32")
   (UNSPEC_VFWIMMACC_I8_BS16 "vfwimmacc_i8_bs16")
   (UNSPEC_VFQWIMMACC_I8_BS32 "vfqwimmacc_i8_bs32")
   (UNSPEC_VFQWIMMACC_I8_BS16 "vfqwimmacc_i8_bs16")
   (UNSPEC_VF8WIMMACC_I8_BS32 "vf8wimmacc_i8_bs32")
   (UNSPEC_VF8WIMMACC_I8_BS16 "vf8wimmacc_i8_bs16")])

(define_int_attr ime_mmacc_insn
  [(UNSPEC_VMMACC_S "vmmacc") (UNSPEC_VMMACC_U "vmmacc")
   (UNSPEC_VMMACC_SU "vmmacc") (UNSPEC_VMMACC_US "vmmacc")
   (UNSPEC_VWMMACC_S "vwmmacc") (UNSPEC_VWMMACC_U "vwmmacc")
   (UNSPEC_VWMMACC_SU "vwmmacc") (UNSPEC_VWMMACC_US "vwmmacc")
   (UNSPEC_VQWMMACC_S "vqwmmacc") (UNSPEC_VQWMMACC_U "vqwmmacc")
   (UNSPEC_VQWMMACC_SU "vqwmmacc") (UNSPEC_VQWMMACC_US "vqwmmacc")
   (UNSPEC_V8WMMACC_S "v8wmmacc") (UNSPEC_V8WMMACC_U "v8wmmacc")
   (UNSPEC_V8WMMACC_SU "v8wmmacc") (UNSPEC_V8WMMACC_US "v8wmmacc")
   (UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 "vfmmacc")
   (UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2 "vfmmacc")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 "vfmmacc")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2 "vfmmacc")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 "vfmmacc")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2 "vfmmacc")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 "vfmmacc")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2 "vfmmacc")
   (UNSPEC_VFWMMACC_F16 "vfwmmacc") (UNSPEC_VFWMMACC_BF16 "vfwmmacc")
   (UNSPEC_VFWMMACC_F32 "vfwmmacc")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3 "vfwmmacc")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2 "vfwmmacc")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 "vfwmmacc")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2 "vfwmmacc")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3 "vfwmmacc")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2 "vfwmmacc")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 "vfwmmacc")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2 "vfwmmacc")
   (UNSPEC_VFQWMMACC_F16 "vfqwmmacc") (UNSPEC_VFQWMMACC_BF16 "vfqwmmacc")
   (UNSPEC_VFQWMMACC_F8E4M3 "vfqwmmacc") (UNSPEC_VFQWMMACC_F8E5M2 "vfqwmmacc")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "vfqwmmacc")
   (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "vfqwmmacc")
   (UNSPEC_VF8WMMACC_F8E4M3 "vf8wmmacc") (UNSPEC_VF8WMMACC_F8E5M2 "vf8wmmacc")
   (UNSPEC_VF8WMMACC_F8E4M3_E5M2 "vf8wmmacc")
   (UNSPEC_VF8WMMACC_F8E5M2_E4M3 "vf8wmmacc")
   (UNSPEC_VFWMMACC_F16_BS32 "vfwmmacc")
   (UNSPEC_VFWMMACC_F16_BS16 "vfwmmacc")
   (UNSPEC_VFQWMMACC_F8E4M3_BS32 "vfqwmmacc")
   (UNSPEC_VFQWMMACC_F8E4M3_BS16 "vfqwmmacc")
   (UNSPEC_VF8WMMACC_F8E4M3_BS32 "vf8wmmacc")
   (UNSPEC_VF8WMMACC_F8E4M3_BS16 "vf8wmmacc")
   (UNSPEC_VFWIMMACC_I8_BS32 "vfwimmacc")
   (UNSPEC_VFWIMMACC_I8_BS16 "vfwimmacc")
   (UNSPEC_VFQWIMMACC_I8_BS32 "vfqwimmacc")
   (UNSPEC_VFQWIMMACC_I8_BS16 "vfqwimmacc")
   (UNSPEC_VF8WIMMACC_I8_BS32 "vf8wimmacc")
   (UNSPEC_VF8WIMMACC_I8_BS16 "vf8wimmacc")])

;; The widening factor W of each integer multiply-accumulate and the
;; signedness of its inputs, as the ime_altfmt value.
(define_int_attr ime_int_w
  [(UNSPEC_VMMACC_S "1") (UNSPEC_VMMACC_U "1")
   (UNSPEC_VMMACC_SU "1") (UNSPEC_VMMACC_US "1")
   (UNSPEC_VWMMACC_S "2") (UNSPEC_VWMMACC_U "2")
   (UNSPEC_VWMMACC_SU "2") (UNSPEC_VWMMACC_US "2")
   (UNSPEC_VQWMMACC_S "4") (UNSPEC_VQWMMACC_U "4")
   (UNSPEC_VQWMMACC_SU "4") (UNSPEC_VQWMMACC_US "4")
   (UNSPEC_V8WMMACC_S "8") (UNSPEC_V8WMMACC_U "8")
   (UNSPEC_V8WMMACC_SU "8") (UNSPEC_V8WMMACC_US "8")])

(define_int_attr ime_int_altfmt
  [(UNSPEC_VMMACC_S "ss") (UNSPEC_VMMACC_U "uu")
   (UNSPEC_VMMACC_SU "su") (UNSPEC_VMMACC_US "us")
   (UNSPEC_VWMMACC_S "ss") (UNSPEC_VWMMACC_U "uu")
   (UNSPEC_VWMMACC_SU "su") (UNSPEC_VWMMACC_US "us")
   (UNSPEC_VQWMMACC_S "ss") (UNSPEC_VQWMMACC_U "uu")
   (UNSPEC_VQWMMACC_SU "su") (UNSPEC_VQWMMACC_US "us")
   (UNSPEC_V8WMMACC_S "ss") (UNSPEC_V8WMMACC_U "uu")
   (UNSPEC_V8WMMACC_SU "su") (UNSPEC_V8WMMACC_US "us")])

;; The accumulator element mode of each FP multiply-accumulate, and the
;; ime_fp_altfmt value of the unscaled OFP8 ones, whose formats share
;; QImode.
(define_int_attr ime_fp_acc
  [(UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 "QI") (UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2 "QI")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 "QI") (UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2 "QI")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 "QI") (UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2 "QI")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 "QI") (UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2 "QI")
   (UNSPEC_VFWMMACC_F16 "SF") (UNSPEC_VFWMMACC_BF16 "SF")
   (UNSPEC_VFWMMACC_F32 "DF")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3 "HF") (UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2 "HF")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 "HF") (UNSPEC_VFWMMACC_OFP8_FP16_E5M2 "HF")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3 "BF") (UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2 "BF")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 "BF") (UNSPEC_VFWMMACC_OFP8_BF16_E5M2 "BF")
   (UNSPEC_VFQWMMACC_F16 "DF") (UNSPEC_VFQWMMACC_BF16 "DF")
   (UNSPEC_VFQWMMACC_F8E4M3 "SF") (UNSPEC_VFQWMMACC_F8E5M2 "SF")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "SF") (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "SF")
   (UNSPEC_VF8WMMACC_F8E4M3 "DF") (UNSPEC_VF8WMMACC_F8E5M2 "DF")
   (UNSPEC_VF8WMMACC_F8E4M3_E5M2 "DF") (UNSPEC_VF8WMMACC_F8E5M2_E4M3 "DF")
   (UNSPEC_VFWMMACC_F16_BS32 "SF")
   (UNSPEC_VFWMMACC_F16_BS16 "SF")
   (UNSPEC_VFQWMMACC_F8E4M3_BS32 "SF")
   (UNSPEC_VFQWMMACC_F8E4M3_BS16 "SF")
   (UNSPEC_VF8WMMACC_F8E4M3_BS32 "DF")
   (UNSPEC_VF8WMMACC_F8E4M3_BS16 "DF")
   (UNSPEC_VFWIMMACC_I8_BS32 "HF")
   (UNSPEC_VFWIMMACC_I8_BS16 "HF")
   (UNSPEC_VFQWIMMACC_I8_BS32 "SF")
   (UNSPEC_VFQWIMMACC_I8_BS16 "SF")
   (UNSPEC_VF8WIMMACC_I8_BS32 "DF")
   (UNSPEC_VF8WIMMACC_I8_BS16 "DF")])

(define_int_attr ime_fp_mmacc_altfmt
  [(UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 "e4m3_e4m3_e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2 "e4m3_e5m2_e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 "e5m2_e4m3_e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2 "e5m2_e5m2_e4m3")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 "e4m3_e4m3_e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2 "e4m3_e5m2_e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 "e5m2_e4m3_e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2 "e5m2_e5m2_e5m2")
   (UNSPEC_VFWMMACC_F16 "none") (UNSPEC_VFWMMACC_BF16 "none")
   (UNSPEC_VFWMMACC_F32 "none")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3 "e4m3_e4m3_e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2 "e4m3_e5m2_e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 "e5m2_e4m3_e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2 "e5m2_e5m2_e4m3")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3 "e4m3_e4m3_e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2 "e4m3_e5m2_e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 "e5m2_e4m3_e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2 "e5m2_e5m2_e5m2")
   (UNSPEC_VFQWMMACC_F16 "none") (UNSPEC_VFQWMMACC_BF16 "none")
   (UNSPEC_VFQWMMACC_F8E4M3 "e4m3_e4m3_e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2 "e5m2_e5m2_e4m3")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "e4m3_e5m2_e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "e5m2_e4m3_e4m3")
   (UNSPEC_VF8WMMACC_F8E4M3 "e4m3_e4m3_e4m3")
   (UNSPEC_VF8WMMACC_F8E5M2 "e5m2_e5m2_e4m3")
   (UNSPEC_VF8WMMACC_F8E4M3_E5M2 "e4m3_e5m2_e4m3")
   (UNSPEC_VF8WMMACC_F8E5M2_E4M3 "e5m2_e4m3_e4m3")])

;; Integer accumulator modes.  Fractional LMULs are not IME accumulators.
(define_mode_iterator IME_INT_ACC
  [RVVM1QI RVVM2QI RVVM4QI RVVM8QI RVVM16QI
   RVVM1HI RVVM2HI RVVM4HI RVVM8HI RVVM16HI
   RVVM1SI RVVM2SI RVVM4SI RVVM8SI RVVM16SI
   (RVVM1DI "TARGET_VECTOR_ELEN_64") (RVVM2DI "TARGET_VECTOR_ELEN_64")
   (RVVM4DI "TARGET_VECTOR_ELEN_64") (RVVM8DI "TARGET_VECTOR_ELEN_64")
   (RVVM16DI "TARGET_VECTOR_ELEN_64")])

;; FP16, BF16, FP32 and FP64 accumulator modes.
(define_mode_iterator VFMMACC_ACC
  [(RVVM1HF "TARGET_ZVFH") (RVVM2HF "TARGET_ZVFH")
   (RVVM4HF "TARGET_ZVFH") (RVVM8HF "TARGET_ZVFH")
   (RVVM16HF "TARGET_ZVFH")
   (RVVM1BF "TARGET_VECTOR_ELEN_BF_16") (RVVM2BF "TARGET_VECTOR_ELEN_BF_16")
   (RVVM4BF "TARGET_VECTOR_ELEN_BF_16") (RVVM8BF "TARGET_VECTOR_ELEN_BF_16")
   (RVVM16BF "TARGET_VECTOR_ELEN_BF_16")
   RVVM1SF RVVM2SF RVVM4SF RVVM8SF RVVM16SF
   (RVVM1DF "TARGET_VECTOR_ELEN_64") (RVVM2DF "TARGET_VECTOR_ELEN_64")
   (RVVM4DF "TARGET_VECTOR_ELEN_64") (RVVM8DF "TARGET_VECTOR_ELEN_64")
   (RVVM16DF "TARGET_VECTOR_ELEN_64")])

;; FP accumulator modes, including the OFP8 ones, which are QImode.
(define_mode_iterator IME_FP_ACC
  [VFMMACC_ACC RVVM1QI RVVM2QI RVVM4QI RVVM8QI RVVM16QI])

;; FP32 accumulator modes.
(define_mode_iterator VF32_ACC [RVVM1SF RVVM2SF RVVM4SF RVVM8SF RVVM16SF])

;; Element modes at LMUL >= 1 for the tile load/store patterns.  The
;; IME spec admits LMUL in {1, 2, 4, 8}; fractional LMULs are not
;; expressible in the tile register-group geometry and are excluded.
;; Includes FP modes as well: vmtl/vmts/vmttl/vmtts treat memory as
;; opaque bytes, so loading/storing FP data is bit-identical to the
;; integer case and differs only in the C-level type.
(define_mode_iterator VI_LMUL1 [RVVM1QI RVVM2QI RVVM4QI RVVM8QI
				RVVM1HI RVVM2HI RVVM4HI RVVM8HI
				RVVM1SI RVVM2SI RVVM4SI RVVM8SI
				RVVM1DI RVVM2DI RVVM4DI RVVM8DI
				(RVVM1HF "TARGET_VECTOR_ELEN_FP_16")
				(RVVM2HF "TARGET_VECTOR_ELEN_FP_16")
				(RVVM4HF "TARGET_VECTOR_ELEN_FP_16")
				(RVVM8HF "TARGET_VECTOR_ELEN_FP_16")
				(RVVM1BF "TARGET_VECTOR_ELEN_BF_16")
				(RVVM2BF "TARGET_VECTOR_ELEN_BF_16")
				(RVVM4BF "TARGET_VECTOR_ELEN_BF_16")
				(RVVM8BF "TARGET_VECTOR_ELEN_BF_16")
				(RVVM1SF "TARGET_VECTOR_ELEN_FP_32")
				(RVVM2SF "TARGET_VECTOR_ELEN_FP_32")
				(RVVM4SF "TARGET_VECTOR_ELEN_FP_32")
				(RVVM8SF "TARGET_VECTOR_ELEN_FP_32")
				(RVVM1DF "TARGET_VECTOR_ELEN_FP_64")
				(RVVM2DF "TARGET_VECTOR_ELEN_FP_64")
				(RVVM4DF "TARGET_VECTOR_ELEN_FP_64")
				(RVVM8DF "TARGET_VECTOR_ELEN_FP_64")])

;; ---------------------------------------------------------------------------
;; Register-form vsetvl for IME instructions
;; ---------------------------------------------------------------------------
;;
;; The RISC-V Vector spec defines three forms of vtype setup:
;;
;;   vsetvli  rd, rs1, vtypei     -- AVL from rs1, vtype from immediate
;;   vsetivli rd, uimm,  vtypei   -- AVL from 5-bit immediate, vtype from immediate
;;   vsetvl   rd, rs1,   rs2      -- AVL from rs1, vtype from rs2 (register)
;;
;; Only the third form can set the IME-specific upper vtype bits (lambda,
;; altfmt_A, altfmt_B, altfmt, bs), because they do not fit in the 11-bit
;; vtypei immediate encoding used by the first two.
;;
;; The existing @vsetvl<mode> / @vsetvl_discard_result<mode> patterns emit
;; the immediate form.  This pattern adds the register form.
;;
;; Post-reload constraint: the vsetvl pass creates IME setup insns
;; post-reload and cannot allocate fresh pseudo-registers.  We work
;; around this by taking the pre-computed full vtype value as a
;; const_int operand and reusing the destination VL register (operand
;; 0) as scratch for the "li" that loads the constant.  The vsetvl
;; instruction then reads operand 0 as its vtype source and writes the
;; resulting VL back into operand 0, which is fine because the incoming
;; const_int is dead once we have built vtype.
;;
;; Operands:
;;   0: destination VL register (also used as the "li" scratch)
;;   1: AVL -- register or zero for VLMAX
;;   2: full vtype value as a const_int (synthesised by the vsetvl
;;      pass from the static sew/vlmul/ta/ma/IME tuple)
;;   3: packed IME demand tuple (const_int).  Metadata only: the asm
;;      template does not reference operand 3; the vsetvl pass reads it
;;      to recover the full `ime_vtype_req' (mul_c, input LMUL, widen_w,
;;      altfmt_*, bs, lambda_log2).  Packing layout: see
;;      `pack_ime_req_fields' in riscv-vsetvl.cc.
(define_insn "@vsetvl_ime<mode>"
  [(set (match_operand:P 0 "register_operand"             "=&r")
	(unspec:P [(match_operand:P 1 "pmode_reg_or_0_operand" "rJ")
		   (match_operand 2 "const_int_operand"    " i")
		   (match_operand 3 "const_int_operand"    " i")]
		  UNSPEC_VSETVL_IME))
   (set (reg:SI VL_REGNUM)
	(unspec:SI [(match_dup 1)
		    (match_dup 2)] UNSPEC_VSETVL_IME))
   (set (reg:SI VTYPE_REGNUM)
	(unspec:SI [(match_dup 2)] UNSPEC_VSETVL_IME))]
  "TARGET_VECTOR"
  "li\t%0,%2\n\tvsetvl\t%0,%z1,%0"
  [(set_attr "type" "vsetvl")
   (set_attr "mode" "<MODE>")])

;; Register-form vsetvl for IME with a register vtype operand.
;;
;; Unlike @vsetvl_ime<mode> (which takes a const_int vtype that it loads
;; via "li"), this variant takes the full vtype value already in a
;; register.  It is used by the pre-reload runtime-lambda expansion path:
;; the intrinsic expander computes the vtype value (including the
;; lambda field derived from a runtime csrr vlenb) into a pseudo, then
;; emits this pattern to execute the register-form vsetvl.
;;
;; Operands:
;;   0: destination VL register
;;   1: AVL -- register or zero for VLMAX
;;   2: full vtype value in a register
;;   3: packed IME demand tuple (const_int).  Metadata only: the asm
;;      template does not reference operand 3; the vsetvl pass reads it
;;      to recover the full `ime_vtype_req' so this insn appears as a
;;      VALID, IME-demand-carrying vsetvl source (rather than UNKNOWN)
;;      and downstream IME consumers see their demand already satisfied.
;;      Packing layout: see `pack_ime_req_fields' in riscv-vsetvl.cc.
(define_insn "@vsetvl_ime_reg<mode>"
  [(set (match_operand:P 0 "register_operand"             "=r")
	(unspec:P [(match_operand:P 1 "pmode_reg_or_0_operand" "rJ")
		   (match_operand:P 2 "register_operand"  "r")
		   (match_operand 3 "const_int_operand"   "i")]
	      UNSPEC_VSETVL_IME))
   (set (reg:SI VL_REGNUM)
	(unspec:SI [(match_dup 1) (match_dup 2)] UNSPEC_VSETVL_IME))
   (set (reg:SI VTYPE_REGNUM)
	(unspec:SI [(match_dup 2)] UNSPEC_VSETVL_IME))]
  "TARGET_VECTOR"
  "vsetvl\t%0,%z1,%2"
  [(set_attr "type" "rdvl")
   (set_attr "mode" "<MODE>")])

;; Read back the hardware vtype CSR.  Used by the runtime-lambda WARL
;; verification sequence to compare the written vtype against what the
;; hardware actually accepted, and by the vsetlambda RMW.
;;
;; This is an unspec_volatile (rather than a plain unspec) so the
;; csrr cannot be reordered past surrounding vtype writers.  Before
;; the vsetvl pass runs, the placeholder pattern
;; `vsetvl_no_side_effects' does not DEF VTYPE_REGNUM in dataflow
;; terms; a non-volatile read could therefore be scheduled above
;; such placeholders and observe a stale vtype after the placeholder
;; is later materialised as a real vsetvli at the placeholder's
;; original site.  Marking the read volatile pins it in source order.
(define_insn "@read_vtype<mode>"
  [(set (match_operand:P 0 "register_operand" "=r")
	(unspec_volatile:P [(reg:SI VTYPE_REGNUM)] UNSPECV_READ_VTYPE))]
  "TARGET_VECTOR"
  "csrr\t%0,vtype"
  [(set_attr "type" "rdvl")
   (set_attr "mode" "<MODE>")])

;; Install a new vtype value while preserving VL.  Used by the
;; `__riscv_vsetlambda' expander: the expander reads vtype, masks off
;; the lambda field, ORs in the new 3-bit encoding, and emits this
;; pattern to commit the result.  The `vsetvl x0, x0, %0' form
;; preserves VL (rd == rs1 == x0) -- only vtype is rewritten.
;;
;; Operand 0: full vtype value to install (in a register).
;; Operand 1: 3-bit lambda encoding hint (const_int 1..7), or 0 if the
;;            install was synthesised from a runtime argument and the
;;            compile-time encoding is unknown.  This is a metadata
;;            operand only -- the assembly output ignores it -- but
;;            the vsetvl pass reads it to identify the install as a
;;            DEF of vtype.lambda with a statically known value.
;;
;; The unspec_volatile prevents the optimizer from reordering vtype
;; writes against surrounding vector operations.
(define_insn "@riscv_vsetlambda_install<mode>"
  [(set (reg:SI VTYPE_REGNUM)
	(unspec_volatile:SI
	  [(match_operand:P 0 "register_operand"  "r")
	   (match_operand   1 "const_int_operand" "i")]
	  UNSPECV_VSETLAMBDA))]
  "TARGET_VECTOR && IN_RANGE (INTVAL (operands[1]), 0, 7)"
  "vsetvl\tx0,x0,%0"
  [(set_attr "type" "vsetvl")
   (set_attr "mode" "<MODE>")])

;; Lambda-preserving vsetvl.  Reads current vtype.lambda dynamically
;; and merges it with the static (SEW, LMUL, ta, ma, altfmt, bs) bits
;; supplied in operand 2, then issues a register-form vsetvl that
;; writes the resulting vtype value while computing VL from operand 1
;; (the AVL).
;;
;; Used by the vsetvl pass when it needs to insert a vsetvli before
;; an IME tile/mmacc consumer that follows a runtime-arg
;; `__riscv_vsetlambda' (lambda lattice reach: DEFINED_RUNTIME).  A
;; plain vsetvli would WARL-clobber the user's lambda; this pattern
;; transplants the live vtype.lambda field into the new vtype.
;;
;; Operands:
;;   0: destination VL register (also used as the scratch for csrr
;;      and the result of the bit-manipulation chain)
;;   1: AVL -- register or zero for VLMAX
;;   2: static vtype bits as const_int, with the lambda field == 0
;;      (the SEW/LMUL/ta/ma/altfmt/bs encoding from the consumer's
;;      demand)
;;
;; Scratch:
;;   3: additional GPR for the mask and the static-bits load
;;
;; Asm template (RV64; RV32 differs only in the shift amount):
;;   csrr   %0, vtype                ; %0 = current vtype
;;   li     %3, 7
;;   slli   %3, %3, XLEN-4           ; %3 = lambda mask = 0x7 << (XLEN-4)
;;   and    %0, %0, %3               ; %0 = vtype & lambda_mask (only λ bits set)
;;   li     %3, %c2                  ; %3 = static_bits (λ field == 0)
;;   or     %0, %0, %3               ; %0 = combined vtype
;;   vsetvl %0, %z1, %0              ; install and produce VL
(define_insn "@riscv_vsetvl_preserve_lambda<mode>"
  [(set (match_operand:P 0 "register_operand"            "=&r")
	(unspec_volatile:P
	  [(match_operand:P 1 "pmode_reg_or_0_operand"   " rJ")
	   (match_operand   2 "const_int_operand"        "  i")
	   (reg:SI VTYPE_REGNUM)]
	  UNSPECV_VSETVL_PRESERVE_LAMBDA))
   (set (reg:SI VL_REGNUM)
	(unspec_volatile:SI [(match_dup 1) (match_dup 2)
			     (reg:SI VTYPE_REGNUM)]
			    UNSPECV_VSETVL_PRESERVE_LAMBDA))
   (set (reg:SI VTYPE_REGNUM)
	(unspec_volatile:SI [(match_dup 2) (reg:SI VTYPE_REGNUM)]
			    UNSPECV_VSETVL_PRESERVE_LAMBDA))
   (clobber (match_operand:P 3 "register_operand"        "=&r"))]
  "TARGET_VECTOR"
  {
    return TARGET_64BIT
      ? "csrr\t%0,vtype\n\t"
	"li\t%3,7\n\t"
	"slli\t%3,%3,60\n\t"
	"and\t%0,%0,%3\n\t"
	"li\t%3,%2\n\t"
	"or\t%0,%0,%3\n\t"
	"vsetvl\t%0,%z1,%0"
      : "csrr\t%0,vtype\n\t"
	"li\t%3,7\n\t"
	"slli\t%3,%3,28\n\t"
	"and\t%0,%0,%3\n\t"
	"li\t%3,%2\n\t"
	"or\t%0,%0,%3\n\t"
	"vsetvl\t%0,%z1,%0";
  }
  [(set_attr "type" "vsetvl")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmm - integer matrix multiply-accumulate (vmmacc.vv, vwmmacc.vv,
;; vqwmmacc.vv and v8wmmacc.vv)
;; ---------------------------------------------------------------------------
;;
;; vd += matmul (vs1, vs2).  The accumulator vd has SEW-bit elements and
;; MUL_C registers: the mode of the pattern.  The inputs have SEW/W-bit
;; elements and an LMUL of 1, 2, 4 or 8 that is independent of MUL_C.
;; Their operands have no mode, so that one pattern covers every input
;; LMUL; the vsetvl pass reads the input LMUL and W from the mode of
;; operand 2.  The UNSPEC selects the instruction and the signedness of
;; the inputs, which the vsetvl pass sets in altfmt_A and altfmt_B from
;; the ime_altfmt attribute.
;;
;; No mask, no merge operand: IME multiply-accumulate instructions do not
;; support element-level masking or tail policies in the standard RVV sense.
(define_insn "@pred_ime_mmacc_<ime_mmacc><mode>"
  [(set (match_operand:IME_INT_ACC 0 "register_operand" "=&vr")
	(if_then_else:IME_INT_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:IME_INT_ACC
	    [(match_operand:IME_INT_ACC 1 "register_operand" "  0")
	     (match_operand 2 "ime_mmacc_input_operand" " vr")
	     (match_operand 3 "ime_mmacc_input_operand" " vr")]
	    IME_INT_MMACC)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "riscv_zvvm_int_mm_p (<MODE>mode, <ime_int_w>)"
  "<ime_mmacc_insn>.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<ime_int_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmtls / Zvvmttls - tile load (vmtl.v, vmttl.v) and store (vmts.v, vmtts.v)
;; ---------------------------------------------------------------------------
;;
;; The two last operands are the lambda immediate (0, or N for the ",LN"
;; forms) and the storage element width S of the _as_e{S} forms (0 for
;; the element width).  S sets the SEW and is at least the element width.

(define_int_iterator IME_TILE_LOAD [UNSPEC_VMTL UNSPEC_VMTTL])
(define_int_iterator IME_TILE_STORE [UNSPEC_VMTS UNSPEC_VMTTS])

(define_int_attr ime_tile [(UNSPEC_VMTL "vmtl") (UNSPEC_VMTTL "vmttl")
			   (UNSPEC_VMTS "vmts") (UNSPEC_VMTTS "vmtts")])

(define_int_attr ime_tile_cond [(UNSPEC_VMTL "TARGET_ZVVMTLS")
				(UNSPEC_VMTTL "TARGET_ZVVMTTLS")
				(UNSPEC_VMTS "TARGET_ZVVMTLS")
				(UNSPEC_VMTTS "TARGET_ZVVMTTLS")])

;; Operand 3 is a memory_operand representing the base "(rs1)" so the
;; RTL aliasing framework knows this is a memory read.  Operand 4 is
;; the element-count stride delivered to rs2.
(define_insn "@pred_ime_<ime_tile><mode>"
  [(set (match_operand:VI_LMUL1 0 "register_operand"           "=vr")
	(if_then_else:VI_LMUL1
	  (unspec:<VM>
	    [(match_operand:<VM> 1 "vector_mask_operand"     "vmWc1")
	     (match_operand 5 "vector_length_operand"        "   rK")
	     (match_operand 6 "const_int_operand"            "    i")
	     (match_operand 7 "const_int_operand"            "    i")
	     (match_operand 8 "const_int_operand"            "    i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VI_LMUL1
	    [(match_operand:VI_LMUL1 3 "memory_operand"      "    m")
	     (mem:BLK (scratch))
	     (match_operand 4 "register_operand"             "    r")
	     (match_operand 9 "const_int_operand"            "    i")
	     (match_operand 10 "const_int_operand"           "    i")]
	    IME_TILE_LOAD)
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))]
  "<ime_tile_cond>
   && GET_MODE_UNIT_BITSIZE (<MODE>mode) <= (INTVAL (operands[10])
					      ? INTVAL (operands[10]) : 64)"
  {
    return (INTVAL (operands[9])
	    ? "<ime_tile>.v\t%0,%3,%4,L%9%p1" : "<ime_tile>.v\t%0,%3,%4%p1");
  }
  [(set_attr "type" "vlds")
   (set_attr "ime_tile" "yes")
   (set_attr "mode" "<MODE>")
   (set (attr "sew")
	(symbol_ref "INTVAL (operands[10]) ? INTVAL (operands[10])
		     : GET_MODE_UNIT_BITSIZE (<MODE>mode)"))])

;; Operand layout follows @pred_strided_store: 0 is the memory
;; operand, 1 is the mask, 2 is the stride register (rs2), 3 is the
;; vector data source (vs3).
(define_insn "@pred_ime_<ime_tile><mode>"
  [(set (mem:BLK (scratch))
	(unspec:BLK
	  [(match_operand:VI_LMUL1 0 "memory_operand"         "    m")
	   (unspec:<VM>
	     [(match_operand:<VM> 1 "vector_mask_operand" "vmWc1")
	      (match_operand 4 "vector_length_operand"   "   rK")
	      (match_operand 5 "const_int_operand"       "    i")
	      (reg:SI VL_REGNUM)
	      (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	   (match_operand 2 "register_operand"            "    r")
	   (match_operand:VI_LMUL1 3 "register_operand"   "   vr")
	   (match_operand 6 "const_int_operand"           "    i")
	   (match_operand 7 "const_int_operand"           "    i")]
	  IME_TILE_STORE))]
  "<ime_tile_cond>
   && GET_MODE_UNIT_BITSIZE (<MODE>mode) <= (INTVAL (operands[7])
					      ? INTVAL (operands[7]) : 64)"
  {
    return (INTVAL (operands[6])
	    ? "<ime_tile>.v\t%3,%0,%2,L%6%p1" : "<ime_tile>.v\t%3,%0,%2%p1");
  }
  [(set_attr "type" "vsts")
   (set_attr "ime_tile" "yes")
   (set_attr "mode" "<MODE>")
   (set (attr "sew")
	(symbol_ref "INTVAL (operands[7]) ? INTVAL (operands[7])
		     : GET_MODE_UNIT_BITSIZE (<MODE>mode)"))
   (set (attr "avl_type_idx") (const_int 5))])

;; ---------------------------------------------------------------------------
;; Zvvfmm - FP matrix multiply-accumulate (vfmmacc.vv, vfwmmacc.vv,
;; vfqwmmacc.vv and vf8wmmacc.vv)
;; ---------------------------------------------------------------------------
;;
;; As for the integer instructions, the input operands have no mode and
;; the vsetvl pass reads the input LMUL and W from operand 2.  It sets
;; altfmt_A, altfmt_B and altfmt for the BF16 inputs and accumulators
;; from their modes, and for the OFP8 formats, which share QImode, from
;; the ime_fp_altfmt attribute.

;; vfmmacc.vv with accumulator and inputs of the same format.
(define_insn "@pred_ime_mmacc_<ime_mmacc><mode>"
  [(set (match_operand:VFMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VFMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)
	     (reg:SI FRM_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VFMMACC_ACC
	    [(match_operand:VFMMACC_ACC 1 "register_operand" "  0")
	     (match_operand 2 "ime_mmacc_input_operand" " vr")
	     (match_operand 3 "ime_mmacc_input_operand" " vr")]
	    IME_VFMMACC)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "riscv_zvvm_fp_mm_p (-1, <MODE>mode, 0)"
  "vfmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")
   (set (attr "frm_mode") (symbol_ref "riscv_vector::FRM_DYN"))])

;; The other formats; the UNSPEC selects the instruction and the
;; accumulator element mode.
(define_insn "@pred_ime_mmacc_<ime_mmacc><mode>"
  [(set (match_operand:IME_FP_ACC 0 "register_operand" "=&vr")
	(if_then_else:IME_FP_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)
	     (reg:SI FRM_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:IME_FP_ACC
	    [(match_operand:IME_FP_ACC 1 "register_operand" "  0")
	     (match_operand 2 "ime_mmacc_input_operand" " vr")
	     (match_operand 3 "ime_mmacc_input_operand" " vr")]
	    IME_FP_MMACC)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "GET_MODE_INNER (<MODE>mode) == <ime_fp_acc>mode
   && riscv_zvvm_fp_mm_p (ZVVM_FMT_<ime_fp_in>, <MODE>mode, 0)"
  "<ime_mmacc_insn>.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_fp_altfmt" "<ime_fp_mmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")
   (set (attr "frm_mode") (symbol_ref "riscv_vector::FRM_DYN"))])

;; vf8wmmacc OFP4_E2M1 -> FP32

(define_insn "@pred_vf8wmmacc_f4e2m1<mode>"
  [(set (match_operand:VF32_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF32_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)
	     (reg:SI FRM_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF32_ACC
	    [(match_operand:VF32_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    UNSPEC_VF8WMMACC_F4E2M1)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "riscv_zvvm_fp_mm_p (ZVVM_FMT_OFP4, <MODE>mode, 0)"
  "vf8wmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")
   (set (attr "frm_mode") (symbol_ref "riscv_vector::FRM_DYN"))])

;; ---------------------------------------------------------------------------
;; Microscaled FP matrix multiply-accumulate
;; ---------------------------------------------------------------------------
;;
;; The microscaled versions of vfwmmacc, vfqwmmacc and vf8wmmacc, and
;; the integer-input vfwimmacc, vfqwimmacc and vf8wimmacc, at BS=32 and
;; BS=16.  The block scales (vuint16m1_t) must be in v0: the expander
;; moves them there and the pattern refers to v0 through a use.  The
;; UNSPEC selects the instruction, the accumulator element mode and the
;; bs vtype bit.
(define_insn "@pred_ime_mmacc_<ime_mmacc><mode>"
  [(set (match_operand:IME_FP_ACC 0 "register_operand" "=&vr")
	(if_then_else:IME_FP_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)
	     (reg:SI FRM_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:IME_FP_ACC
	    [(match_operand:IME_FP_ACC 1 "register_operand" "  0")
	     (match_operand 2 "ime_mmacc_input_operand" " vr")
	     (match_operand 3 "ime_mmacc_input_operand" " vr")]
	    IME_SCALED_MMACC)
	  (match_dup 1)))
   (use (reg:RVVM1HI V0_REGNUM))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "GET_MODE_INNER (<MODE>mode) == <ime_fp_acc>mode
   && riscv_zvvm_fp_mm_p (ZVVM_FMT_<ime_fp_in>, <MODE>mode, <ime_scaled_bs>)"
  "<ime_mmacc_insn>.vv\t%0,%2,%3,v0.t"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_bs" "bs<ime_scaled_bs>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")
   (set (attr "frm_mode") (symbol_ref "riscv_vector::FRM_DYN"))])

;; -----------------------------------------------------------------------
;; ---- IME pair / unpair primitives
;; -----------------------------------------------------------------------
;;
;; pair (m8_lo, m8_hi)        -> m16   (compose accumulator)
;; unpairlo (m16)             -> m8    (lower half of m16)
;; unpairhi (m16)             -> m8    (upper half of m16)
;;
;; The IME spec restricts m16 to mmacc + pair/unpair operations.
;; pair/unpair are purely a reinterpretation of the register state:
;; an m16 already occupies sixteen vector registers at a 16-aligned
;; base (v0..v15 or v16..v31 -- enforced by VM16_REGS), and its two
;; m8 halves naturally live at base..base+7 and base+8..base+15.
;; When register allocation happens to place the operands at the
;; right offset, the primitive lowers to nothing; otherwise it
;; lowers to one (unpair) or up to two (pair) `vmv8r.v' moves.
;;
;; UNSPEC scheme rather than SUBREG because RTL subreg validation
;; rejects the m8-of-m16 case for VLA register pseudos before RA
;; (validate_subreg's per-mode block check fails on the polynomial
;; size relationship), and modelling each primitive as a recognised
;; insn lets the post-RA splitter make the no-op-vs-vmv8r decision
;; against the actual hard-register placements.

(define_insn_and_split "@vunpairlo<mode>"
  [(set (match_operand:<VM16_HALF> 0 "register_operand" "=vr")
	(unspec:<VM16_HALF>
	  [(match_operand:VM16 1 "register_operand" "vr")]
	  UNSPEC_VUNPAIRLO))]
  "TARGET_ZVVM_MM"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  unsigned dregno = REGNO (operands[0]);
  unsigned sregno = REGNO (operands[1]);
  /* The lower half of an m16 group at v<sregno> is the m8 group at
     v<sregno>; if the destination is already there the primitive is
     a no-op.  Emit a blockage so the original `"#"' insn is replaced
     -- `try_split' treats a zero-insn split as a failure, leaving
     the `"#"' template insn in the stream and ICEing at final.  */
  if (dregno == sregno)
    {
      emit_insn (gen_blockage ());
      DONE;
    }
  emit_move_insn (gen_rtx_REG (<VM16_HALF>mode, dregno),
		  gen_rtx_REG (<VM16_HALF>mode, sregno));
  DONE;
}
  [(set_attr "type" "vmov")
   (set_attr "mode" "<VM16_HALF>")])

(define_insn_and_split "@vunpairhi<mode>"
  [(set (match_operand:<VM16_HALF> 0 "register_operand" "=vr")
	(unspec:<VM16_HALF>
	  [(match_operand:VM16 1 "register_operand" "vr")]
	  UNSPEC_VUNPAIRHI))]
  "TARGET_ZVVM_MM"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  unsigned dregno = REGNO (operands[0]);
  unsigned sregno = REGNO (operands[1]);
  /* The upper half of an m16 group at v<sregno> is the m8 group at
     v<sregno+8>; no-op when the destination already lands there.
     Same blockage trick as `vunpairlo' above.  */
  if (dregno == sregno + 8)
    {
      emit_insn (gen_blockage ());
      DONE;
    }
  emit_move_insn (gen_rtx_REG (<VM16_HALF>mode, dregno),
		  gen_rtx_REG (<VM16_HALF>mode, sregno + 8));
  DONE;
}
  [(set_attr "type" "vmov")
   (set_attr "mode" "<VM16_HALF>")])

(define_insn_and_split "@vpair<mode>"
  [(set (match_operand:VM16 0 "register_operand"        "=vr")
	(unspec:VM16
	  [(match_operand:<VM16_HALF> 1 "register_operand" "vr")
	   (match_operand:<VM16_HALF> 2 "register_operand" "vr")]
	  UNSPEC_VPAIR))
   (clobber (match_scratch:<VM16_HALF> 3                "=&vr"))]
  "TARGET_ZVVM_MM"
  "#"
  "&& reload_completed"
  [(const_int 0)]
{
  unsigned dregno = REGNO (operands[0]);
  unsigned loregno = REGNO (operands[1]);
  unsigned hiregno = REGNO (operands[2]);
  /* The composed m16 is the register group at v<dregno>; its lower
     half lives at v<dregno> and its upper half at v<dregno + 8>.
     Emit a vmv8r.v for each half whose source is not already in
     place.

     Ordering matters when one half's source overlaps the other
     half's destination:

       * loregno == dregno + 8   -- the lo source occupies the
	 upper-half slot of dst.  Do the hi-half move FIRST so that
	 the subsequent lo-half move can still read the lo source
	 out of dregno + 8 before it is overwritten.

       * hiregno == dregno       -- the hi source occupies the
	 lower-half slot of dst.  Do the hi-half move FIRST so the
	 hi source is captured to dregno + 8 before the lo-half
	 move clobbers dregno.

       * loregno == dregno + 8 AND hiregno == dregno -- a true swap
	 of the two halves.  Break the dependency cycle through the
	 m8 scratch: stash the hi source, write the lo half, then
	 restore the hi source into the upper half.  */
  bool need_lo = (loregno != dregno);
  bool need_hi = (hiregno != dregno + 8);
  bool full_swap = (loregno == dregno + 8 && hiregno == dregno);

  /* No-op: both halves are already in place.  `try_split' treats a
     splitter that emits zero insns as a failure and leaves the
     original `"#"' template insn in the stream, ICEing at final.
     Emit a blockage so the original is replaced by a real (but
     codegen-free) insn -- post-RA cleanup removes it.  */
  if (!need_lo && !need_hi && !full_swap)
    {
      emit_insn (gen_blockage ());
      DONE;
    }

  if (full_swap)
    {
      rtx scratch = operands[3];
      emit_move_insn (scratch,
		      gen_rtx_REG (<VM16_HALF>mode, hiregno));
      emit_move_insn (gen_rtx_REG (<VM16_HALF>mode, dregno),
		      gen_rtx_REG (<VM16_HALF>mode, loregno));
      emit_move_insn (gen_rtx_REG (<VM16_HALF>mode, dregno + 8),
		      scratch);
      DONE;
    }

  bool order_hi_first
    = need_hi && (hiregno == dregno
		  || (need_lo && loregno == dregno + 8));

  if (order_hi_first)
    {
      emit_move_insn (gen_rtx_REG (<VM16_HALF>mode, dregno + 8),
		      gen_rtx_REG (<VM16_HALF>mode, hiregno));
      if (need_lo)
	emit_move_insn (gen_rtx_REG (<VM16_HALF>mode, dregno),
			gen_rtx_REG (<VM16_HALF>mode, loregno));
    }
  else
    {
      if (need_lo)
	emit_move_insn (gen_rtx_REG (<VM16_HALF>mode, dregno),
			gen_rtx_REG (<VM16_HALF>mode, loregno));
      if (need_hi)
	emit_move_insn (gen_rtx_REG (<VM16_HALF>mode, dregno + 8),
			gen_rtx_REG (<VM16_HALF>mode, hiregno));
    }
  DONE;
}
  [(set_attr "type" "vmov")
   (set_attr "mode" "<VM16_HALF>")])
