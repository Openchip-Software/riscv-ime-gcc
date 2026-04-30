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

;; UNSPEC placeholders for the IME instructions.  Instruction patterns are
;; added as each IME instruction family is implemented; see the plan in
;; transient-marinating-cake.md for the rollout order.

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
  UNSPEC_VMTL_L1     ;; vmtl.v ,L1
  UNSPEC_VMTL_L2     ;; vmtl.v ,L2
  UNSPEC_VMTL_L4     ;; vmtl.v ,L4
  UNSPEC_VMTL_L8     ;; vmtl.v ,L8
  UNSPEC_VMTL_L16    ;; vmtl.v ,L16
  UNSPEC_VMTL_L32    ;; vmtl.v ,L32
  UNSPEC_VMTL_L64    ;; vmtl.v ,L64
  UNSPEC_VMTS_L1     ;; vmts.v ,L1
  UNSPEC_VMTS_L2
  UNSPEC_VMTS_L4
  UNSPEC_VMTS_L8
  UNSPEC_VMTS_L16
  UNSPEC_VMTS_L32
  UNSPEC_VMTS_L64
  UNSPEC_VMTTL_L1    ;; vmttl.v ,L1
  UNSPEC_VMTTL_L2
  UNSPEC_VMTTL_L4
  UNSPEC_VMTTL_L8
  UNSPEC_VMTTL_L16
  UNSPEC_VMTTL_L32
  UNSPEC_VMTTL_L64
  UNSPEC_VMTTS_L1    ;; vmtts.v ,L1
  UNSPEC_VMTTS_L2
  UNSPEC_VMTTS_L4
  UNSPEC_VMTTS_L8
  UNSPEC_VMTTS_L16
  UNSPEC_VMTTS_L32
  UNSPEC_VMTTS_L64

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
  ;; W=2 vfwmmacc OFP8 -> fp16 / bf16: 4 input combinations per acc
  ;; format = 8 UNSPECs.  Acc altfmt is encoded in the UNSPEC name
  ;; (FP16 or BF16); the mode iterator on each MD pattern only spans
  ;; the matching acc element type (HF or BF).
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

  ;; Microscaled (§8) FP-input scaled variants: same mnemonic as §7 but
  ;; with block-scale operand in v0 and the bs vtype bit set.
  UNSPEC_VFWMMACC_F16_BS32	;; vfwmmacc.vv scaled, FP16->FP32, BS=32
  UNSPEC_VFWMMACC_F16_BS16	;; vfwmmacc.vv scaled, FP16->FP32, BS=16
  UNSPEC_VFQWMMACC_F8E4M3_BS32	;; vfqwmmacc.vv scaled, OFP8_E4M3->FP32, BS=32
  UNSPEC_VFQWMMACC_F8E4M3_BS16	;; vfqwmmacc.vv scaled, OFP8_E4M3->FP32, BS=16
  UNSPEC_VF8WMMACC_F8E4M3_BS32	;; vf8wmmacc.vv scaled, OFP8_E4M3->FP64, BS=32
  UNSPEC_VF8WMMACC_F8E4M3_BS16	;; vf8wmmacc.vv scaled, OFP8_E4M3->FP64, BS=16

  ;; Microscaled (§8) integer-input variants: new instruction mnemonics.
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

  ;; Read back the hardware vtype CSR (for WARL verification).
  UNSPEC_READ_VTYPE
])

;; Signedness encoding for the two integer-input operands of IME
;; multiply-accumulate instructions, as it maps to the vtype altfmt_A /
;; altfmt_B bits that the vsetvl pass establishes.  Both same-sign
;; (ss, uu) and mixed-sign (su, us) cases are supported for vmmacc and
;; the widening variants.
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

;; Block scale size for microscaled (§8) variants.
;; "none" means not a microscaled instruction (bs bit irrelevant).
;; "bs16" and "bs32" set the bs vtype bit accordingly.
(define_attr "ime_bs" "none,bs16,bs32"
  (const_string "none"))

;; Operand index of the hidden "vtype source" operand on IME consumer
;; patterns.  Default INVALID_ATTRIBUTE (no such operand).  Otherwise
;; this is the index of a `(use (match_operand N "vtype_op_operand"))'
;; clause -- the operand carries no MD-level mode (the special predicate
;; checks it accepts only a Pmode register or const0_rtx); the rtx the
;; expander threads through is in Pmode (DImode on RV64, SImode on RV32).
;; clause in the pattern's parallel; the runtime-VLEN IME path
;; threads the full_vtype rtx through this operand so the vsetvl
;; pass can re-anchor IME vtype right before the consumer after
;; spill-induced clobbers.  Static-VLEN consumers also carry this
;; operand but with `const0_rtx' as a placeholder (the pass
;; synthesises its own const vtype via `vsetvl_ime' for the static
;; path rather than reading this operand).
(define_attr "ime_vtype_op_idx" ""
  (const_int INVALID_ATTRIBUTE))

(define_int_iterator VMMACC_SU
  [UNSPEC_VMMACC_S UNSPEC_VMMACC_U UNSPEC_VMMACC_SU UNSPEC_VMMACC_US])

(define_int_attr vmmacc_su_name [(UNSPEC_VMMACC_S "_s")
				 (UNSPEC_VMMACC_U "_u")
				 (UNSPEC_VMMACC_SU "_su")
				 (UNSPEC_VMMACC_US "_us")])

(define_int_attr vmmacc_altfmt [(UNSPEC_VMMACC_S "ss")
				(UNSPEC_VMMACC_U "uu")
				(UNSPEC_VMMACC_SU "su")
				(UNSPEC_VMMACC_US "us")])

(define_int_iterator VWMMACC_SUSUUS
  [UNSPEC_VWMMACC_S UNSPEC_VWMMACC_U UNSPEC_VWMMACC_SU UNSPEC_VWMMACC_US])

(define_int_attr vwmmacc_su_name [(UNSPEC_VWMMACC_S  "_s")
				  (UNSPEC_VWMMACC_U  "_u")
				  (UNSPEC_VWMMACC_SU "_su")
				  (UNSPEC_VWMMACC_US "_us")])

(define_int_attr vwmmacc_altfmt [(UNSPEC_VWMMACC_S  "ss")
				 (UNSPEC_VWMMACC_U  "uu")
				 (UNSPEC_VWMMACC_SU "su")
				 (UNSPEC_VWMMACC_US "us")])

(define_int_iterator VQWMMACC_SUSUUS
  [UNSPEC_VQWMMACC_S UNSPEC_VQWMMACC_U UNSPEC_VQWMMACC_SU UNSPEC_VQWMMACC_US])

(define_int_attr vqwmmacc_su_name [(UNSPEC_VQWMMACC_S  "_s")
				   (UNSPEC_VQWMMACC_U  "_u")
				   (UNSPEC_VQWMMACC_SU "_su")
				   (UNSPEC_VQWMMACC_US "_us")])

(define_int_attr vqwmmacc_altfmt [(UNSPEC_VQWMMACC_S  "ss")
				  (UNSPEC_VQWMMACC_U  "uu")
				  (UNSPEC_VQWMMACC_SU "su")
				  (UNSPEC_VQWMMACC_US "us")])

(define_int_iterator V8WMMACC_SUSUUS
  [UNSPEC_V8WMMACC_S UNSPEC_V8WMMACC_U UNSPEC_V8WMMACC_SU UNSPEC_V8WMMACC_US])

(define_int_attr v8wmmacc_su_name [(UNSPEC_V8WMMACC_S  "_s")
				   (UNSPEC_V8WMMACC_U  "_u")
				   (UNSPEC_V8WMMACC_SU "_su")
				   (UNSPEC_V8WMMACC_US "_us")])

(define_int_attr v8wmmacc_altfmt [(UNSPEC_V8WMMACC_S  "ss")
				  (UNSPEC_V8WMMACC_U  "uu")
				  (UNSPEC_V8WMMACC_SU "su")
				  (UNSPEC_V8WMMACC_US "us")])

;; Accumulator modes for the W=2 widening integer matrix
;; multiply-accumulate (vwmmacc.vv).  The input modes are half the
;; accumulator SEW at LMUL=1 (see VI_TRUNC_LMUL1 below).  Fractional
;; accumulator LMULs are omitted for now; they correspond to
;; MUL_C < 1 configurations that only arise on unusual (VLEN, lambda)
;; targets.
(define_mode_iterator VWMMACC_ACC [RVVM8HI RVVM4HI RVVM2HI RVVM1HI
				   RVVM8SI RVVM4SI RVVM2SI RVVM1SI
				   RVVM8DI RVVM4DI RVVM2DI RVVM1DI])

;; Map each VWMMACC_ACC accumulator mode to the LMUL=1 form at half its
;; SEW: the mode used for vs1 / vs2 operands.
(define_mode_attr VI_TRUNC_LMUL1 [(RVVM8HI "RVVM1QI") (RVVM4HI "RVVM1QI")
				  (RVVM2HI "RVVM1QI") (RVVM1HI "RVVM1QI")
				  (RVVM8SI "RVVM1HI") (RVVM4SI "RVVM1HI")
				  (RVVM2SI "RVVM1HI") (RVVM1SI "RVVM1HI")
				  (RVVM8DI "RVVM1SI") (RVVM4DI "RVVM1SI")
				  (RVVM2DI "RVVM1SI") (RVVM1DI "RVVM1SI")])

;; Accumulator modes for W=4 (vqwmmacc.vv).  Inputs are SEW/4 at LMUL=1.
;; Only SI and DI accumulators have a valid quad-truncated integer type
;; (QI and HI respectively).
(define_mode_iterator VQWMMACC_ACC [RVVM8SI RVVM4SI RVVM2SI RVVM1SI
				    RVVM8DI RVVM4DI RVVM2DI RVVM1DI])

(define_mode_attr VI_QUAD_TRUNC_LMUL1 [(RVVM8SI "RVVM1QI") (RVVM4SI "RVVM1QI")
				       (RVVM2SI "RVVM1QI") (RVVM1SI "RVVM1QI")
				       (RVVM8DI "RVVM1HI") (RVVM4DI "RVVM1HI")
				       (RVVM2DI "RVVM1HI") (RVVM1DI "RVVM1HI")])

;; Accumulator modes for W=8 (v8wmmacc.vv).  Inputs are SEW/8 at LMUL=1.
;; Only DI accumulators have a valid oct-truncated integer type (QI).
(define_mode_iterator V8WMMACC_ACC [RVVM8DI RVVM4DI RVVM2DI RVVM1DI])

(define_mode_attr VI_OCT_TRUNC_LMUL1 [(RVVM8DI "RVVM1QI") (RVVM4DI "RVVM1QI")
				      (RVVM2DI "RVVM1QI") (RVVM1DI "RVVM1QI")])

;; Source-LMUL mode_attrs for the W=2 widening mmacc _lm{N} variants.
;; Each maps an accumulator mode to its narrower-SEW (SEW/2) sibling at
;; LMUL=N (independent of MUL_C).  Mirrors VI_TRUNC_LMUL1 but the
;; destination's LMUL is N rather than 1.
(define_mode_attr VI_TRUNC_LMUL2 [(RVVM8HI "RVVM2QI") (RVVM4HI "RVVM2QI")
				  (RVVM2HI "RVVM2QI") (RVVM1HI "RVVM2QI")
				  (RVVM8SI "RVVM2HI") (RVVM4SI "RVVM2HI")
				  (RVVM2SI "RVVM2HI") (RVVM1SI "RVVM2HI")
				  (RVVM8DI "RVVM2SI") (RVVM4DI "RVVM2SI")
				  (RVVM2DI "RVVM2SI") (RVVM1DI "RVVM2SI")])

(define_mode_attr VI_TRUNC_LMUL4 [(RVVM8HI "RVVM4QI") (RVVM4HI "RVVM4QI")
				  (RVVM2HI "RVVM4QI") (RVVM1HI "RVVM4QI")
				  (RVVM8SI "RVVM4HI") (RVVM4SI "RVVM4HI")
				  (RVVM2SI "RVVM4HI") (RVVM1SI "RVVM4HI")
				  (RVVM8DI "RVVM4SI") (RVVM4DI "RVVM4SI")
				  (RVVM2DI "RVVM4SI") (RVVM1DI "RVVM4SI")])

(define_mode_attr VI_TRUNC_LMUL8 [(RVVM8HI "RVVM8QI") (RVVM4HI "RVVM8QI")
				  (RVVM2HI "RVVM8QI") (RVVM1HI "RVVM8QI")
				  (RVVM8SI "RVVM8HI") (RVVM4SI "RVVM8HI")
				  (RVVM2SI "RVVM8HI") (RVVM1SI "RVVM8HI")
				  (RVVM8DI "RVVM8SI") (RVVM4DI "RVVM8SI")
				  (RVVM2DI "RVVM8SI") (RVVM1DI "RVVM8SI")])

;; W=4 (vqwmmacc.vv) source-LMUL mode_attrs.  Mirrors
;; VI_QUAD_TRUNC_LMUL1 but the destination's LMUL is N.
(define_mode_attr VI_QUAD_TRUNC_LMUL2 [(RVVM8SI "RVVM2QI") (RVVM4SI "RVVM2QI")
				       (RVVM2SI "RVVM2QI") (RVVM1SI "RVVM2QI")
				       (RVVM8DI "RVVM2HI") (RVVM4DI "RVVM2HI")
				       (RVVM2DI "RVVM2HI") (RVVM1DI "RVVM2HI")])

(define_mode_attr VI_QUAD_TRUNC_LMUL4 [(RVVM8SI "RVVM4QI") (RVVM4SI "RVVM4QI")
				       (RVVM2SI "RVVM4QI") (RVVM1SI "RVVM4QI")
				       (RVVM8DI "RVVM4HI") (RVVM4DI "RVVM4HI")
				       (RVVM2DI "RVVM4HI") (RVVM1DI "RVVM4HI")])

(define_mode_attr VI_QUAD_TRUNC_LMUL8 [(RVVM8SI "RVVM8QI") (RVVM4SI "RVVM8QI")
				       (RVVM2SI "RVVM8QI") (RVVM1SI "RVVM8QI")
				       (RVVM8DI "RVVM8HI") (RVVM4DI "RVVM8HI")
				       (RVVM2DI "RVVM8HI") (RVVM1DI "RVVM8HI")])

;; W=8 (v8wmmacc.vv) source-LMUL mode_attrs.  Mirrors
;; VI_OCT_TRUNC_LMUL1 but the destination's LMUL is N.  Only DI
;; accumulators are valid (QI input only).
(define_mode_attr VI_OCT_TRUNC_LMUL2 [(RVVM8DI "RVVM2QI") (RVVM4DI "RVVM2QI")
				      (RVVM2DI "RVVM2QI") (RVVM1DI "RVVM2QI")])

(define_mode_attr VI_OCT_TRUNC_LMUL4 [(RVVM8DI "RVVM4QI") (RVVM4DI "RVVM4QI")
				      (RVVM2DI "RVVM4QI") (RVVM1DI "RVVM4QI")])

(define_mode_attr VI_OCT_TRUNC_LMUL8 [(RVVM8DI "RVVM8QI") (RVVM4DI "RVVM8QI")
				      (RVVM2DI "RVVM8QI") (RVVM1DI "RVVM8QI")])

;; ---------------------------------------------------------------------------
;; FP mode iterators and int iterators for Zvvfmm
;; ---------------------------------------------------------------------------

;; FP accumulator modes at LMUL >= 1 for vfmmacc (W=1, same-format).
;; Includes HF, BF, SF, DF.
(define_mode_iterator VFMMACC_ACC
  [(RVVM1HF "TARGET_ZVFH") (RVVM2HF "TARGET_ZVFH")
   (RVVM4HF "TARGET_ZVFH") (RVVM8HF "TARGET_ZVFH")
   (RVVM1BF "TARGET_VECTOR_ELEN_BF_16") (RVVM2BF "TARGET_VECTOR_ELEN_BF_16")
   (RVVM4BF "TARGET_VECTOR_ELEN_BF_16") (RVVM8BF "TARGET_VECTOR_ELEN_BF_16")
   RVVM1SF RVVM2SF RVVM4SF RVVM8SF
   (RVVM1DF "TARGET_VECTOR_ELEN_64") (RVVM2DF "TARGET_VECTOR_ELEN_64")
   (RVVM4DF "TARGET_VECTOR_ELEN_64") (RVVM8DF "TARGET_VECTOR_ELEN_64")])

;; Map VFMMACC_ACC modes to their LMUL=1 counterpart (same SEW).
;; V_LMUL1 does not cover BF modes, so we define a local attr.
(define_mode_attr VF_LMUL1
  [(RVVM1HF "RVVM1HF") (RVVM2HF "RVVM1HF")
   (RVVM4HF "RVVM1HF") (RVVM8HF "RVVM1HF")
   (RVVM1BF "RVVM1BF") (RVVM2BF "RVVM1BF")
   (RVVM4BF "RVVM1BF") (RVVM8BF "RVVM1BF")
   (RVVM1SF "RVVM1SF") (RVVM2SF "RVVM1SF")
   (RVVM4SF "RVVM1SF") (RVVM8SF "RVVM1SF")
   (RVVM1DF "RVVM1DF") (RVVM2DF "RVVM1DF")
   (RVVM4DF "RVVM1DF") (RVVM8DF "RVVM1DF")])

;; FP32 accumulator modes at LMUL >= 1 (for W=2 FP16/BF16, W=4 OFP8).
(define_mode_iterator VF32_ACC [RVVM1SF RVVM2SF RVVM4SF RVVM8SF])

;; OFP8 accumulator modes for W=1 vfmmacc OFP8 -> OFP8.  Both E4M3 and
;; E5M2 share QImode; the acc format is selected by the
;; ime_fp_altfmt attribute on each pattern.
(define_mode_iterator VFMMACC_OFP8_ACC [RVVM1QI RVVM2QI RVVM4QI RVVM8QI])

;; FP64 accumulator modes at LMUL >= 1 (for W=2 FP32, W=4 FP16/BF16, W=8 OFP8).
(define_mode_iterator VF64_ACC
  [(RVVM1DF "TARGET_VECTOR_ELEN_64") (RVVM2DF "TARGET_VECTOR_ELEN_64")
   (RVVM4DF "TARGET_VECTOR_ELEN_64") (RVVM8DF "TARGET_VECTOR_ELEN_64")])

;; Int iterator for vfqwmmacc OFP8 variants (E4M3/E5M2 vs1 x E4M3/E5M2
;; vs2 -> FP32).  Acc is FP32 which has no altfmt freedom; the third
;; token of the ime_fp_altfmt encoding is therefore always e4m3 (= 0).
;; Four entries cover the 2x2 cross-product of input formats.
(define_int_iterator VFQWMMACC_OFP8
  [UNSPEC_VFQWMMACC_F8E4M3 UNSPEC_VFQWMMACC_F8E5M2
   UNSPEC_VFQWMMACC_F8E4M3_E5M2 UNSPEC_VFQWMMACC_F8E5M2_E4M3])

(define_int_attr vfqwmmacc_ofp8_name
  [(UNSPEC_VFQWMMACC_F8E4M3      "_f8e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2      "_f8e5m2")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "_f8e4m3_f8e5m2")
   (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "_f8e5m2_f8e4m3")])

(define_int_attr vfqwmmacc_ofp8_altfmt
  [(UNSPEC_VFQWMMACC_F8E4M3      "e4m3_e4m3_e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2      "e5m2_e5m2_e4m3")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "e4m3_e5m2_e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "e5m2_e4m3_e4m3")])

;; The vs1 / vs2 input modes vary per variant; map the UNSPEC to the
;; element format of each input so the pattern can pick the right
;; match_operand inner mode.  Both inputs share RVVM*QI at the GCC
;; level (OFP8 storage is QImode); the ime_fp_altfmt attribute is the
;; only thing telling them apart at the vsetvl pass.
(define_int_attr vfqwmmacc_ofp8_vs1
  [(UNSPEC_VFQWMMACC_F8E4M3      "e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2      "e5m2")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "e5m2")])

(define_int_attr vfqwmmacc_ofp8_vs2
  [(UNSPEC_VFQWMMACC_F8E4M3      "e4m3")
   (UNSPEC_VFQWMMACC_F8E5M2      "e5m2")
   (UNSPEC_VFQWMMACC_F8E4M3_E5M2 "e5m2")
   (UNSPEC_VFQWMMACC_F8E5M2_E4M3 "e4m3")])

;; Int iterator for vf8wmmacc OFP8 variants (W=8, FP64 acc).  Same
;; 2x2 input cross-product as vfqwmmacc.
(define_int_iterator VF8WMMACC_OFP8
  [UNSPEC_VF8WMMACC_F8E4M3 UNSPEC_VF8WMMACC_F8E5M2
   UNSPEC_VF8WMMACC_F8E4M3_E5M2 UNSPEC_VF8WMMACC_F8E5M2_E4M3])

(define_int_attr vf8wmmacc_ofp8_name
  [(UNSPEC_VF8WMMACC_F8E4M3      "_f8e4m3")
   (UNSPEC_VF8WMMACC_F8E5M2      "_f8e5m2")
   (UNSPEC_VF8WMMACC_F8E4M3_E5M2 "_f8e4m3_f8e5m2")
   (UNSPEC_VF8WMMACC_F8E5M2_E4M3 "_f8e5m2_f8e4m3")])

(define_int_attr vf8wmmacc_ofp8_altfmt
  [(UNSPEC_VF8WMMACC_F8E4M3      "e4m3_e4m3_e4m3")
   (UNSPEC_VF8WMMACC_F8E5M2      "e5m2_e5m2_e4m3")
   (UNSPEC_VF8WMMACC_F8E4M3_E5M2 "e4m3_e5m2_e4m3")
   (UNSPEC_VF8WMMACC_F8E5M2_E4M3 "e5m2_e4m3_e4m3")])

;; W=1 vfmmacc OFP8 -> OFP8: 8 UNSPECs covering the full
;; (altfmt_A, altfmt_B, altfmt) cube.  All 8 share a single MD pattern
;; template; the int iterator drives the per-instance UNSPEC and the
;; matching ime_fp_altfmt enumerator.  Acc mode is RVVM1QI in stage 3a
;; (MVP); m2/m4/m8 acc is a follow-up that requires registering more
;; OFP8 vector types.
(define_int_iterator VFMMACC_OFP8
  [UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2
   UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2
   UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2
   UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2])

;; Suffix order on the user-visible intrinsic name is acc_vs1_vs2;
;; the int_attr below mirrors it for the MD pattern name (the "acc"
;; token here also picks the C return type via the type set).
(define_int_attr vfmmacc_ofp8_name
  [(UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 "_f8e4m3_f8e4m3_f8e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2 "_f8e4m3_f8e4m3_f8e5m2")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 "_f8e4m3_f8e5m2_f8e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2 "_f8e4m3_f8e5m2_f8e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 "_f8e5m2_f8e4m3_f8e4m3")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2 "_f8e5m2_f8e4m3_f8e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 "_f8e5m2_f8e5m2_f8e4m3")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2 "_f8e5m2_f8e5m2_f8e5m2")])

;; The ime_fp_altfmt encoding follows the (altfmt_A, altfmt_B, altfmt)
;; order: vs1, vs2, acc.  The UNSPEC name is acc_vs1_vs2; rewrite it
;; into vs1_vs2_acc here.  Each token is e4m3 (= 0) or e5m2 (= 1).
(define_int_attr vfmmacc_ofp8_altfmt
  [(UNSPEC_VFMMACC_F8_E4M3_E4M3_E4M3 "e4m3_e4m3_e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E4M3_E5M2 "e4m3_e5m2_e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E4M3 "e5m2_e4m3_e4m3")
   (UNSPEC_VFMMACC_F8_E4M3_E5M2_E5M2 "e5m2_e5m2_e4m3")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E4M3 "e4m3_e4m3_e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E4M3_E5M2 "e4m3_e5m2_e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E4M3 "e5m2_e4m3_e5m2")
   (UNSPEC_VFMMACC_F8_E5M2_E5M2_E5M2 "e5m2_e5m2_e5m2")])

;; W=2 vfwmmacc OFP8 -> fp16 / bf16: separate int iterators per acc
;; format so the mode iterator's acc type matches the encoded altfmt
;; bit.  Each iterator has 4 entries (the (vs1, vs2) input cube at the
;; selected acc altfmt).
(define_int_iterator VFWMMACC_OFP8_FP16
  [UNSPEC_VFWMMACC_OFP8_FP16_E4M3      UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2
   UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 UNSPEC_VFWMMACC_OFP8_FP16_E5M2])

(define_int_iterator VFWMMACC_OFP8_BF16
  [UNSPEC_VFWMMACC_OFP8_BF16_E4M3      UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2
   UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 UNSPEC_VFWMMACC_OFP8_BF16_E5M2])

(define_int_attr vfwmmacc_ofp8_fp16_name
  [(UNSPEC_VFWMMACC_OFP8_FP16_E4M3      "_f8e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2 "_f8e4m3_f8e5m2")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 "_f8e5m2_f8e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2      "_f8e5m2")])

(define_int_attr vfwmmacc_ofp8_bf16_name
  [(UNSPEC_VFWMMACC_OFP8_BF16_E4M3      "_f8e4m3")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2 "_f8e4m3_f8e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 "_f8e5m2_f8e4m3")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2      "_f8e5m2")])

;; ime_fp_altfmt mapping per UNSPEC: vs1, vs2, acc tokens.  acc is
;; e4m3 (= 0 = fp16) for FP16 UNSPECs and e5m2 (= 1 = bf16) for BF16
;; UNSPECs.
(define_int_attr vfwmmacc_ofp8_fp16_altfmt
  [(UNSPEC_VFWMMACC_OFP8_FP16_E4M3      "e4m3_e4m3_e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E4M3_E5M2 "e4m3_e5m2_e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2_E4M3 "e5m2_e4m3_e4m3")
   (UNSPEC_VFWMMACC_OFP8_FP16_E5M2      "e5m2_e5m2_e4m3")])

(define_int_attr vfwmmacc_ofp8_bf16_altfmt
  [(UNSPEC_VFWMMACC_OFP8_BF16_E4M3      "e4m3_e4m3_e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E4M3_E5M2 "e4m3_e5m2_e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2_E4M3 "e5m2_e4m3_e5m2")
   (UNSPEC_VFWMMACC_OFP8_BF16_E5M2      "e5m2_e5m2_e5m2")])

;; Acc mode iterators for the W=2 OFP8 -> fp16 / bf16 patterns.  HF and
;; BF are kept separate so each pattern's acc altfmt bit is unambiguous.
(define_mode_iterator VFW_OFP8_FP16_ACC
  [(RVVM1HF "TARGET_ZVFH") (RVVM2HF "TARGET_ZVFH")
   (RVVM4HF "TARGET_ZVFH") (RVVM8HF "TARGET_ZVFH")])

(define_mode_iterator VFW_OFP8_BF16_ACC
  [(RVVM1BF "TARGET_VECTOR_ELEN_BF_16") (RVVM2BF "TARGET_VECTOR_ELEN_BF_16")
   (RVVM4BF "TARGET_VECTOR_ELEN_BF_16") (RVVM8BF "TARGET_VECTOR_ELEN_BF_16")])

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
(define_insn "@vsetvl_ime<mode>"
  [(set (match_operand:P 0 "register_operand"             "=&r")
	(unspec:P [(match_operand:P 1 "pmode_reg_or_0_operand" "rJ")
		   (match_operand 2 "const_int_operand"    " i")]
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
(define_insn "@vsetvl_ime_reg<mode>"
  [(set (match_operand:P 0 "register_operand"             "=r")
	(unspec:P [(match_operand:P 1 "pmode_reg_or_0_operand" "rJ")
		   (match_operand:P 2 "register_operand"  "r")]
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
;; hardware actually accepted.
(define_insn "@read_vtype<mode>"
  [(set (match_operand:P 0 "register_operand" "=r")
	(unspec:P [(reg:SI VTYPE_REGNUM)] UNSPEC_READ_VTYPE))]
  "TARGET_VECTOR"
  "csrr\t%0,vtype"
  [(set_attr "type" "rdvl")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmm - Vector integer matrix multiply-accumulate, W=1 (no widening)
;; ---------------------------------------------------------------------------
;;
;; vmmacc.vv vd, vs1, vs2 performs an accumulator-style multiply-accumulate:
;;
;;     vd += matmul (vs1, vs2)
;;
;; The accumulator vd has SEW bit elements and uses MUL_C registers (encoded
;; by the register group size of vd in the C intrinsic type).  The inputs
;; vs1/vs2 have the same SEW as the accumulator but use their own LMUL,
;; independent of MUL_C.  This pattern handles the spec-default LMUL=1
;; variant; _lm{N} variants where vs1/vs2 use LMUL=N at the same SEW are
;; expanded by the `@pred_vmmacc<vmmacc_su_name>_lm{2,4,8}<mode>'
;; siblings below.
;;
;; Signed (UNSPEC_VMMACC_S) and unsigned (UNSPEC_VMMACC_U) emit the same
;; vmmacc.vv instruction; the distinction lives entirely in the vtype
;; altfmt_A / altfmt_B bits that the vsetvl pass establishes from the
;; ime_altfmt attribute.
;;
;; No mask, no merge operand: IME multiply-accumulate instructions do not
;; support element-level masking or tail policies in the standard RVV sense.
(define_insn "@pred_vmmacc<vmmacc_su_name><mode>"
  [(set (match_operand:VI 0 "register_operand" "=&vr")
	(if_then_else:VI
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VI
	    [(match_operand:VI         1 "register_operand" "  0")
	     (match_operand:<V_LMUL1>  2 "register_operand" " vr")
	     (match_operand:<V_LMUL1>  3 "register_operand" " vr")] VMMACC_SU)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; Source-LMUL mode_attrs for the W=1 mmacc _lm{N} variants.  Each maps
;; an accumulator mode (any SEW × any MUL_C) to its same-SEW sibling at
;; LMUL=N.  Defined per-LMUL so we can reuse VI as the iterator.
(define_mode_attr V_LMUL2 [
  (RVVM8QI "RVVM2QI") (RVVM4QI "RVVM2QI") (RVVM2QI "RVVM2QI") (RVVM1QI "RVVM2QI")
  (RVVMF2QI "RVVM2QI") (RVVMF4QI "RVVM2QI") (RVVMF8QI "RVVM2QI")
  (RVVM8HI "RVVM2HI") (RVVM4HI "RVVM2HI") (RVVM2HI "RVVM2HI") (RVVM1HI "RVVM2HI")
  (RVVMF2HI "RVVM2HI") (RVVMF4HI "RVVM2HI")
  (RVVM8SI "RVVM2SI") (RVVM4SI "RVVM2SI") (RVVM2SI "RVVM2SI") (RVVM1SI "RVVM2SI")
  (RVVMF2SI "RVVM2SI")
  (RVVM8DI "RVVM2DI") (RVVM4DI "RVVM2DI") (RVVM2DI "RVVM2DI") (RVVM1DI "RVVM2DI")
  (RVVM8HF "RVVM2HF") (RVVM4HF "RVVM2HF") (RVVM2HF "RVVM2HF") (RVVM1HF "RVVM2HF")
  (RVVM8BF "RVVM2BF") (RVVM4BF "RVVM2BF") (RVVM2BF "RVVM2BF") (RVVM1BF "RVVM2BF")
  (RVVM8SF "RVVM2SF") (RVVM4SF "RVVM2SF") (RVVM2SF "RVVM2SF") (RVVM1SF "RVVM2SF")
  (RVVM8DF "RVVM2DF") (RVVM4DF "RVVM2DF") (RVVM2DF "RVVM2DF") (RVVM1DF "RVVM2DF")
])

(define_mode_attr V_LMUL4 [
  (RVVM8QI "RVVM4QI") (RVVM4QI "RVVM4QI") (RVVM2QI "RVVM4QI") (RVVM1QI "RVVM4QI")
  (RVVMF2QI "RVVM4QI") (RVVMF4QI "RVVM4QI") (RVVMF8QI "RVVM4QI")
  (RVVM8HI "RVVM4HI") (RVVM4HI "RVVM4HI") (RVVM2HI "RVVM4HI") (RVVM1HI "RVVM4HI")
  (RVVMF2HI "RVVM4HI") (RVVMF4HI "RVVM4HI")
  (RVVM8SI "RVVM4SI") (RVVM4SI "RVVM4SI") (RVVM2SI "RVVM4SI") (RVVM1SI "RVVM4SI")
  (RVVMF2SI "RVVM4SI")
  (RVVM8DI "RVVM4DI") (RVVM4DI "RVVM4DI") (RVVM2DI "RVVM4DI") (RVVM1DI "RVVM4DI")
  (RVVM8HF "RVVM4HF") (RVVM4HF "RVVM4HF") (RVVM2HF "RVVM4HF") (RVVM1HF "RVVM4HF")
  (RVVM8BF "RVVM4BF") (RVVM4BF "RVVM4BF") (RVVM2BF "RVVM4BF") (RVVM1BF "RVVM4BF")
  (RVVM8SF "RVVM4SF") (RVVM4SF "RVVM4SF") (RVVM2SF "RVVM4SF") (RVVM1SF "RVVM4SF")
  (RVVM8DF "RVVM4DF") (RVVM4DF "RVVM4DF") (RVVM2DF "RVVM4DF") (RVVM1DF "RVVM4DF")
])

(define_mode_attr V_LMUL8 [
  (RVVM8QI "RVVM8QI") (RVVM4QI "RVVM8QI") (RVVM2QI "RVVM8QI") (RVVM1QI "RVVM8QI")
  (RVVMF2QI "RVVM8QI") (RVVMF4QI "RVVM8QI") (RVVMF8QI "RVVM8QI")
  (RVVM8HI "RVVM8HI") (RVVM4HI "RVVM8HI") (RVVM2HI "RVVM8HI") (RVVM1HI "RVVM8HI")
  (RVVMF2HI "RVVM8HI") (RVVMF4HI "RVVM8HI")
  (RVVM8SI "RVVM8SI") (RVVM4SI "RVVM8SI") (RVVM2SI "RVVM8SI") (RVVM1SI "RVVM8SI")
  (RVVMF2SI "RVVM8SI")
  (RVVM8DI "RVVM8DI") (RVVM4DI "RVVM8DI") (RVVM2DI "RVVM8DI") (RVVM1DI "RVVM8DI")
  (RVVM8HF "RVVM8HF") (RVVM4HF "RVVM8HF") (RVVM2HF "RVVM8HF") (RVVM1HF "RVVM8HF")
  (RVVM8BF "RVVM8BF") (RVVM4BF "RVVM8BF") (RVVM2BF "RVVM8BF") (RVVM1BF "RVVM8BF")
  (RVVM8SF "RVVM8SF") (RVVM4SF "RVVM8SF") (RVVM2SF "RVVM8SF") (RVVM1SF "RVVM8SF")
  (RVVM8DF "RVVM8DF") (RVVM4DF "RVVM8DF") (RVVM2DF "RVVM8DF") (RVVM1DF "RVVM8DF")
])

;; W=1 mmacc with vs1/vs2 at LMUL=2.  Same accumulator iterator (VI) and
;; signed/unsigned dispatch (VMMACC_SU) as the default LMUL=1 pattern;
;; only the source mode changes from <V_LMUL1> to <V_LMUL2>.
(define_insn "@pred_vmmacc<vmmacc_su_name>_lm2<mode>"
  [(set (match_operand:VI 0 "register_operand" "=&vr")
	(if_then_else:VI
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VI
	    [(match_operand:VI         1 "register_operand" "  0")
	     (match_operand:<V_LMUL2>  2 "register_operand" " vr")
	     (match_operand:<V_LMUL2>  3 "register_operand" " vr")] VMMACC_SU)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=1 mmacc with vs1/vs2 at LMUL=4.
(define_insn "@pred_vmmacc<vmmacc_su_name>_lm4<mode>"
  [(set (match_operand:VI 0 "register_operand" "=&vr")
	(if_then_else:VI
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VI
	    [(match_operand:VI         1 "register_operand" "  0")
	     (match_operand:<V_LMUL4>  2 "register_operand" " vr")
	     (match_operand:<V_LMUL4>  3 "register_operand" " vr")] VMMACC_SU)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=1 mmacc with vs1/vs2 at LMUL=8.
(define_insn "@pred_vmmacc<vmmacc_su_name>_lm8<mode>"
  [(set (match_operand:VI 0 "register_operand" "=&vr")
	(if_then_else:VI
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VI
	    [(match_operand:VI         1 "register_operand" "  0")
	     (match_operand:<V_LMUL8>  2 "register_operand" " vr")
	     (match_operand:<V_LMUL8>  3 "register_operand" " vr")] VMMACC_SU)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmm - Vector integer matrix multiply-accumulate, W=2 (vwmmacc.vv)
;; ---------------------------------------------------------------------------
;;
;; vwmmacc.vv vd, vs1, vs2 -- accumulator at SEW, inputs at SEW/2 and LMUL=1
;; (independent of MUL_C).  Four sign combinations (ss / uu / su / us) share
;; the same assembly mnemonic; the distinction lives in vtype altfmt_A and
;; altfmt_B, which the vsetvl pass reads from the ime_altfmt attribute.
(define_insn "@pred_vwmmacc<vwmmacc_su_name><mode>"
  [(set (match_operand:VWMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VWMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VWMMACC_ACC
	    [(match_operand:VWMMACC_ACC 1 "register_operand"      "  0")
	     (match_operand:<VI_TRUNC_LMUL1> 2 "register_operand" " vr")
	     (match_operand:<VI_TRUNC_LMUL1> 3 "register_operand" " vr")]
	    VWMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vwmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=2 widening vwmmacc with vs1/vs2 at LMUL=2.  Same accumulator
;; iterator (VWMMACC_ACC) and signed/unsigned dispatch (VWMMACC_SUSUUS)
;; as the default LMUL=1 pattern; only the source mode changes from
;; <VI_TRUNC_LMUL1> to <VI_TRUNC_LMUL2>.
(define_insn "@pred_vwmmacc<vwmmacc_su_name>_lm2<mode>"
  [(set (match_operand:VWMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VWMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VWMMACC_ACC
	    [(match_operand:VWMMACC_ACC 1 "register_operand"      "  0")
	     (match_operand:<VI_TRUNC_LMUL2> 2 "register_operand" " vr")
	     (match_operand:<VI_TRUNC_LMUL2> 3 "register_operand" " vr")]
	    VWMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vwmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=2 widening vwmmacc with vs1/vs2 at LMUL=4.
(define_insn "@pred_vwmmacc<vwmmacc_su_name>_lm4<mode>"
  [(set (match_operand:VWMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VWMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VWMMACC_ACC
	    [(match_operand:VWMMACC_ACC 1 "register_operand"      "  0")
	     (match_operand:<VI_TRUNC_LMUL4> 2 "register_operand" " vr")
	     (match_operand:<VI_TRUNC_LMUL4> 3 "register_operand" " vr")]
	    VWMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vwmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=2 widening vwmmacc with vs1/vs2 at LMUL=8.
(define_insn "@pred_vwmmacc<vwmmacc_su_name>_lm8<mode>"
  [(set (match_operand:VWMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VWMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VWMMACC_ACC
	    [(match_operand:VWMMACC_ACC 1 "register_operand"      "  0")
	     (match_operand:<VI_TRUNC_LMUL8> 2 "register_operand" " vr")
	     (match_operand:<VI_TRUNC_LMUL8> 3 "register_operand" " vr")]
	    VWMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vwmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmm - Vector integer matrix multiply-accumulate, W=4 (vqwmmacc.vv)
;; ---------------------------------------------------------------------------
;;
;; vqwmmacc.vv vd, vs1, vs2 -- accumulator at SEW, inputs at SEW/4 and
;; LMUL=1.  Four sign combinations share the same assembly mnemonic;
;; altfmt_A / altfmt_B in vtype encode the per-side signedness.
(define_insn "@pred_vqwmmacc<vqwmmacc_su_name><mode>"
  [(set (match_operand:VQWMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VQWMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VQWMMACC_ACC
	    [(match_operand:VQWMMACC_ACC 1 "register_operand"           "  0")
	     (match_operand:<VI_QUAD_TRUNC_LMUL1> 2 "register_operand" " vr")
	     (match_operand:<VI_QUAD_TRUNC_LMUL1> 3 "register_operand" " vr")]
	    VQWMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vqwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vqwmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=4 widening vqwmmacc with vs1/vs2 at LMUL=2.
(define_insn "@pred_vqwmmacc<vqwmmacc_su_name>_lm2<mode>"
  [(set (match_operand:VQWMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VQWMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VQWMMACC_ACC
	    [(match_operand:VQWMMACC_ACC 1 "register_operand"           "  0")
	     (match_operand:<VI_QUAD_TRUNC_LMUL2> 2 "register_operand" " vr")
	     (match_operand:<VI_QUAD_TRUNC_LMUL2> 3 "register_operand" " vr")]
	    VQWMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vqwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vqwmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=4 widening vqwmmacc with vs1/vs2 at LMUL=4.
(define_insn "@pred_vqwmmacc<vqwmmacc_su_name>_lm4<mode>"
  [(set (match_operand:VQWMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VQWMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VQWMMACC_ACC
	    [(match_operand:VQWMMACC_ACC 1 "register_operand"           "  0")
	     (match_operand:<VI_QUAD_TRUNC_LMUL4> 2 "register_operand" " vr")
	     (match_operand:<VI_QUAD_TRUNC_LMUL4> 3 "register_operand" " vr")]
	    VQWMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vqwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vqwmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=4 widening vqwmmacc with vs1/vs2 at LMUL=8.
(define_insn "@pred_vqwmmacc<vqwmmacc_su_name>_lm8<mode>"
  [(set (match_operand:VQWMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VQWMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VQWMMACC_ACC
	    [(match_operand:VQWMMACC_ACC 1 "register_operand"           "  0")
	     (match_operand:<VI_QUAD_TRUNC_LMUL8> 2 "register_operand" " vr")
	     (match_operand:<VI_QUAD_TRUNC_LMUL8> 3 "register_operand" " vr")]
	    VQWMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "vqwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vqwmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmm - Vector integer matrix multiply-accumulate, W=8 (v8wmmacc.vv)
;; ---------------------------------------------------------------------------
;;
;; v8wmmacc.vv vd, vs1, vs2 -- accumulator at SEW (DI only), inputs at
;; SEW/8 (QI) and LMUL=1.  Four sign combinations; altfmt_A / altfmt_B
;; encode the per-side signedness.
;; ---------------------------------------------------------------------------
;; Zvvmtls - Order-preserving tile load (vmtl.v), MVP
;; ---------------------------------------------------------------------------
;;
;; vmtl.v vd, (rs1), rs2 -- loads a 2D matrix tile into the vector
;; register group starting at vd.  rs1 is the base address, rs2 is the
;; leading dimension in ELEMENTS (not bytes).  SEW, LMUL, lambda, and
;; ta/ma come from vtype; this MVP pattern covers LMUL=1 only and the
;; default (dynamic-lambda) form without the immediate Lλ override.
;;
;; Operand 3 is a memory_operand representing the base "(rs1)" so the
;; RTL aliasing framework knows this is a memory read.  Operand 4 is
;; the element-count stride delivered to rs2.
(define_insn "@pred_vmtl<mode>"
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
	     (match_operand 4 "register_operand"             "    r")]
	    UNSPEC_VMTL)
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))
   (use (match_operand 9 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMTLS"
  "vmtl.v\t%0,%3,%4%p1"
  [(set_attr "type" "vlds")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "9")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmtls - vmtl.v with immediate "Lλ" override
;; ---------------------------------------------------------------------------
;;
;; Same operand layout as @pred_vmtl<mode> plus an additional const_int
;; operand (9) that carries the lambda immediate value (one of
;; 1/2/4/8/16/32/64).  The immediate overrides the dynamic lambda
;; field of vtype for this instruction only; SEW/LMUL still come from
;; vtype, so the IME vtype machinery still runs.
;; Seven patterns share one template, one per immediate Lλ value.
;; The lambda is encoded in the UNSPEC so the operand layout stays
;; identical to @pred_vmtl<mode> and use_contiguous_load_insn works
;; unchanged.
(define_int_iterator VMTL_L
  [UNSPEC_VMTL_L1 UNSPEC_VMTL_L2 UNSPEC_VMTL_L4 UNSPEC_VMTL_L8
   UNSPEC_VMTL_L16 UNSPEC_VMTL_L32 UNSPEC_VMTL_L64])

(define_int_attr vmtl_L_imm [(UNSPEC_VMTL_L1  "L1")
			     (UNSPEC_VMTL_L2  "L2")
			     (UNSPEC_VMTL_L4  "L4")
			     (UNSPEC_VMTL_L8  "L8")
			     (UNSPEC_VMTL_L16 "L16")
			     (UNSPEC_VMTL_L32 "L32")
			     (UNSPEC_VMTL_L64 "L64")])

(define_int_attr vmtl_L_name [(UNSPEC_VMTL_L1  "_L1")
			      (UNSPEC_VMTL_L2  "_L2")
			      (UNSPEC_VMTL_L4  "_L4")
			      (UNSPEC_VMTL_L8  "_L8")
			      (UNSPEC_VMTL_L16 "_L16")
			      (UNSPEC_VMTL_L32 "_L32")
			      (UNSPEC_VMTL_L64 "_L64")])

;; vmts.v _Lλ
(define_int_iterator VMTS_L
  [UNSPEC_VMTS_L1 UNSPEC_VMTS_L2 UNSPEC_VMTS_L4 UNSPEC_VMTS_L8
   UNSPEC_VMTS_L16 UNSPEC_VMTS_L32 UNSPEC_VMTS_L64])

(define_int_attr vmts_L_imm [(UNSPEC_VMTS_L1  "L1")
			     (UNSPEC_VMTS_L2  "L2")
			     (UNSPEC_VMTS_L4  "L4")
			     (UNSPEC_VMTS_L8  "L8")
			     (UNSPEC_VMTS_L16 "L16")
			     (UNSPEC_VMTS_L32 "L32")
			     (UNSPEC_VMTS_L64 "L64")])

(define_int_attr vmts_L_name [(UNSPEC_VMTS_L1  "_L1")
			      (UNSPEC_VMTS_L2  "_L2")
			      (UNSPEC_VMTS_L4  "_L4")
			      (UNSPEC_VMTS_L8  "_L8")
			      (UNSPEC_VMTS_L16 "_L16")
			      (UNSPEC_VMTS_L32 "_L32")
			      (UNSPEC_VMTS_L64 "_L64")])

;; vmttl.v _Lλ
(define_int_iterator VMTTL_L
  [UNSPEC_VMTTL_L1 UNSPEC_VMTTL_L2 UNSPEC_VMTTL_L4 UNSPEC_VMTTL_L8
   UNSPEC_VMTTL_L16 UNSPEC_VMTTL_L32 UNSPEC_VMTTL_L64])

(define_int_attr vmttl_L_imm [(UNSPEC_VMTTL_L1  "L1")
			      (UNSPEC_VMTTL_L2  "L2")
			      (UNSPEC_VMTTL_L4  "L4")
			      (UNSPEC_VMTTL_L8  "L8")
			      (UNSPEC_VMTTL_L16 "L16")
			      (UNSPEC_VMTTL_L32 "L32")
			      (UNSPEC_VMTTL_L64 "L64")])

(define_int_attr vmttl_L_name [(UNSPEC_VMTTL_L1  "_L1")
			       (UNSPEC_VMTTL_L2  "_L2")
			       (UNSPEC_VMTTL_L4  "_L4")
			       (UNSPEC_VMTTL_L8  "_L8")
			       (UNSPEC_VMTTL_L16 "_L16")
			       (UNSPEC_VMTTL_L32 "_L32")
			       (UNSPEC_VMTTL_L64 "_L64")])

;; vmtts.v _Lλ
(define_int_iterator VMTTS_L
  [UNSPEC_VMTTS_L1 UNSPEC_VMTTS_L2 UNSPEC_VMTTS_L4 UNSPEC_VMTTS_L8
   UNSPEC_VMTTS_L16 UNSPEC_VMTTS_L32 UNSPEC_VMTTS_L64])

(define_int_attr vmtts_L_imm [(UNSPEC_VMTTS_L1  "L1")
			      (UNSPEC_VMTTS_L2  "L2")
			      (UNSPEC_VMTTS_L4  "L4")
			      (UNSPEC_VMTTS_L8  "L8")
			      (UNSPEC_VMTTS_L16 "L16")
			      (UNSPEC_VMTTS_L32 "L32")
			      (UNSPEC_VMTTS_L64 "L64")])

(define_int_attr vmtts_L_name [(UNSPEC_VMTTS_L1  "_L1")
			       (UNSPEC_VMTTS_L2  "_L2")
			       (UNSPEC_VMTTS_L4  "_L4")
			       (UNSPEC_VMTTS_L8  "_L8")
			       (UNSPEC_VMTTS_L16 "_L16")
			       (UNSPEC_VMTTS_L32 "_L32")
			       (UNSPEC_VMTTS_L64 "_L64")])

(define_insn "@pred_vmtl<vmtl_L_name><mode>"
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
	     (match_operand 4 "register_operand"             "    r")]
	    VMTL_L)
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))
   (use (match_operand 9 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMTLS"
  "vmtl.v\t%0,%3,%4,<vmtl_L_imm>%p1"
  [(set_attr "type" "vlds")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "9")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmtls - Order-preserving tile store (vmts.v), MVP
;; ---------------------------------------------------------------------------
;;
;; vmts.v vs3, (rs1), rs2 -- stores the vector register group vs3 to a
;; 2D matrix tile in memory.  Same scope as the vmtl.v MVP: LMUL=1,
;; dynamic lambda, unmasked.
;;
;; Operand layout follows @pred_strided_store: 0 is the memory
;; operand, 1 is the mask, 2 is the stride register (rs2), 3 is the
;; vector data source (vs3).
(define_insn "@pred_vmts<mode>"
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
	   (match_operand:VI_LMUL1 3 "register_operand"   "   vr")]
	  UNSPEC_VMTS))
   (use (match_operand 6 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMTLS"
  "vmts.v\t%3,%0,%2%p1"
  [(set_attr "type" "vsts")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "6")
   (set_attr "mode" "<MODE>")
   (set (attr "avl_type_idx") (const_int 5))])

;; vmts.v with immediate "Lλ" override.  Shares the store-shape
;; operand layout of @pred_vmts<mode>; the lambda is encoded in the
;; UNSPEC via the VMTS_L iterator.
(define_insn "@pred_vmts<vmts_L_name><mode>"
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
	   (match_operand:VI_LMUL1 3 "register_operand"   "   vr")]
	  VMTS_L))
   (use (match_operand 6 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMTLS"
  "vmts.v\t%3,%0,%2,<vmts_L_imm>%p1"
  [(set_attr "type" "vsts")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "6")
   (set_attr "mode" "<MODE>")
   (set (attr "avl_type_idx") (const_int 5))])

;; ---------------------------------------------------------------------------
;; Zvvmttls - Transposing tile load (vmttl.v), MVP
;; ---------------------------------------------------------------------------
;;
;; Same operand layout as vmtl.v; the transposition is entirely an
;; instruction-internal reordering, not visible in RTL.  MVP scope:
;; LMUL=1, dynamic lambda, unmasked.
(define_insn "@pred_vmttl<mode>"
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
	     (match_operand 4 "register_operand"             "    r")]
	    UNSPEC_VMTTL)
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))
   (use (match_operand 9 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMTTLS"
  "vmttl.v\t%0,%3,%4%p1"
  [(set_attr "type" "vlds")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "9")
   (set_attr "mode" "<MODE>")])

;; vmttl.v with immediate "Lλ" override.
(define_insn "@pred_vmttl<vmttl_L_name><mode>"
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
	     (match_operand 4 "register_operand"             "    r")]
	    VMTTL_L)
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))
   (use (match_operand 9 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMTTLS"
  "vmttl.v\t%0,%3,%4,<vmttl_L_imm>%p1"
  [(set_attr "type" "vlds")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "9")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvmttls - Transposing tile store (vmtts.v), MVP
;; ---------------------------------------------------------------------------
(define_insn "@pred_vmtts<mode>"
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
	   (match_operand:VI_LMUL1 3 "register_operand"   "   vr")]
	  UNSPEC_VMTTS))
   (use (match_operand 6 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMTTLS"
  "vmtts.v\t%3,%0,%2%p1"
  [(set_attr "type" "vsts")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "6")
   (set_attr "mode" "<MODE>")
   (set (attr "avl_type_idx") (const_int 5))])

;; vmtts.v with immediate "Lλ" override.
(define_insn "@pred_vmtts<vmtts_L_name><mode>"
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
	   (match_operand:VI_LMUL1 3 "register_operand"   "   vr")]
	  VMTTS_L))
   (use (match_operand 6 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMTTLS"
  "vmtts.v\t%3,%0,%2,<vmtts_L_imm>%p1"
  [(set_attr "type" "vsts")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "6")
   (set_attr "mode" "<MODE>")
   (set (attr "avl_type_idx") (const_int 5))])

(define_insn "@pred_v8wmmacc<v8wmmacc_su_name><mode>"
  [(set (match_operand:V8WMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:V8WMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:V8WMMACC_ACC
	    [(match_operand:V8WMMACC_ACC 1 "register_operand"          "  0")
	     (match_operand:<VI_OCT_TRUNC_LMUL1> 2 "register_operand" " vr")
	     (match_operand:<VI_OCT_TRUNC_LMUL1> 3 "register_operand" " vr")]
	    V8WMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "v8wmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<v8wmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=8 widening v8wmmacc with vs1/vs2 at LMUL=2.
(define_insn "@pred_v8wmmacc<v8wmmacc_su_name>_lm2<mode>"
  [(set (match_operand:V8WMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:V8WMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:V8WMMACC_ACC
	    [(match_operand:V8WMMACC_ACC 1 "register_operand"          "  0")
	     (match_operand:<VI_OCT_TRUNC_LMUL2> 2 "register_operand" " vr")
	     (match_operand:<VI_OCT_TRUNC_LMUL2> 3 "register_operand" " vr")]
	    V8WMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "v8wmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<v8wmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=8 widening v8wmmacc with vs1/vs2 at LMUL=4.
(define_insn "@pred_v8wmmacc<v8wmmacc_su_name>_lm4<mode>"
  [(set (match_operand:V8WMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:V8WMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:V8WMMACC_ACC
	    [(match_operand:V8WMMACC_ACC 1 "register_operand"          "  0")
	     (match_operand:<VI_OCT_TRUNC_LMUL4> 2 "register_operand" " vr")
	     (match_operand:<VI_OCT_TRUNC_LMUL4> 3 "register_operand" " vr")]
	    V8WMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "v8wmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<v8wmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; W=8 widening v8wmmacc with vs1/vs2 at LMUL=8.
(define_insn "@pred_v8wmmacc<v8wmmacc_su_name>_lm8<mode>"
  [(set (match_operand:V8WMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:V8WMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:V8WMMACC_ACC
	    [(match_operand:V8WMMACC_ACC 1 "register_operand"          "  0")
	     (match_operand:<VI_OCT_TRUNC_LMUL8> 2 "register_operand" " vr")
	     (match_operand:<VI_OCT_TRUNC_LMUL8> 3 "register_operand" " vr")]
	    V8WMMACC_SUSUUS)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVMM"
  "v8wmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<v8wmmacc_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvfmm - FP matrix multiply-accumulate, W=1 (vfmmacc.vv)
;; ---------------------------------------------------------------------------
;;
;; vfmmacc.vv vd, vs1, vs2 -- FP accumulator-style multiply-accumulate:
;;   vd += matmul (vs1, vs2)
;; Accumulator and inputs share the same FP type (HF, BF, SF, DF).
;; Inputs always at LMUL=1.
(define_insn "@pred_vfmmacc<mode>"
  [(set (match_operand:VFMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VFMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VFMMACC_ACC
	    [(match_operand:VFMMACC_ACC    1 "register_operand" "  0")
	     (match_operand:<VF_LMUL1>     2 "register_operand" " vr")
	     (match_operand:<VF_LMUL1>     3 "register_operand" " vr")]
	    UNSPEC_VFMMACC)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfmmacc with vs1/vs2 at LMUL=2 (same SEW as accumulator).  Same as
;; @pred_vfmmacc but the source operand mode comes from <V_LMUL2>.
(define_insn "@pred_vfmmacc_lm2<mode>"
  [(set (match_operand:VFMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VFMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VFMMACC_ACC
	    [(match_operand:VFMMACC_ACC    1 "register_operand" "  0")
	     (match_operand:<V_LMUL2>      2 "register_operand" " vr")
	     (match_operand:<V_LMUL2>      3 "register_operand" " vr")]
	    UNSPEC_VFMMACC)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfmmacc with vs1/vs2 at LMUL=4.
(define_insn "@pred_vfmmacc_lm4<mode>"
  [(set (match_operand:VFMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VFMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VFMMACC_ACC
	    [(match_operand:VFMMACC_ACC    1 "register_operand" "  0")
	     (match_operand:<V_LMUL4>      2 "register_operand" " vr")
	     (match_operand:<V_LMUL4>      3 "register_operand" " vr")]
	    UNSPEC_VFMMACC)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfmmacc with vs1/vs2 at LMUL=8.
(define_insn "@pred_vfmmacc_lm8<mode>"
  [(set (match_operand:VFMMACC_ACC 0 "register_operand" "=&vr")
	(if_then_else:VFMMACC_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VFMMACC_ACC
	    [(match_operand:VFMMACC_ACC    1 "register_operand" "  0")
	     (match_operand:<V_LMUL8>      2 "register_operand" " vr")
	     (match_operand:<V_LMUL8>      3 "register_operand" " vr")]
	    UNSPEC_VFMMACC)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfmmacc.vv (W=1) OFP8 -> OFP8: 8 variants over the (acc, vs1, vs2)
;; altfmt cube.  All inputs and the accumulator share RVVM*QI mode at
;; the GCC level; the per-instance UNSPEC and the ime_fp_altfmt
;; attribute distinguish them at the vsetvl pass.
(define_insn "@pred_vfmmacc_ofp8<vfmmacc_ofp8_name><mode>"
  [(set (match_operand:VFMMACC_OFP8_ACC 0 "register_operand" "=&vr")
	(if_then_else:VFMMACC_OFP8_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VFMMACC_OFP8_ACC
	    [(match_operand:VFMMACC_OFP8_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI          2 "register_operand" " vr")
	     (match_operand:RVVM1QI          3 "register_operand" " vr")]
	    VFMMACC_OFP8)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_fp_altfmt" "<vfmmacc_ofp8_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvfmm - FP matrix multiply-accumulate, W=2 (vfwmmacc.vv)
;; ---------------------------------------------------------------------------

;; vfwmmacc FP16 -> FP32
(define_insn "@pred_vfwmmacc_f16<mode>"
  [(set (match_operand:VF32_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF32_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF32_ACC
	    [(match_operand:VF32_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1HF  2 "register_operand" " vr")
	     (match_operand:RVVM1HF  3 "register_operand" " vr")]
	    UNSPEC_VFWMMACC_F16)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfwmmacc BF16 -> FP32
(define_insn "@pred_vfwmmacc_bf16<mode>"
  [(set (match_operand:VF32_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF32_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF32_ACC
	    [(match_operand:VF32_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1BF  2 "register_operand" " vr")
	     (match_operand:RVVM1BF  3 "register_operand" " vr")]
	    UNSPEC_VFWMMACC_BF16)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfwmmacc OFP8 -> FP16: 4 input combinations, acc altfmt = 0
;; (selects fp16 over bf16 at SEW=16).
(define_insn "@pred_vfwmmacc_ofp8_fp16<vfwmmacc_ofp8_fp16_name><mode>"
  [(set (match_operand:VFW_OFP8_FP16_ACC 0 "register_operand" "=&vr")
	(if_then_else:VFW_OFP8_FP16_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VFW_OFP8_FP16_ACC
	    [(match_operand:VFW_OFP8_FP16_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI           2 "register_operand" " vr")
	     (match_operand:RVVM1QI           3 "register_operand" " vr")]
	    VFWMMACC_OFP8_FP16)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_fp_altfmt" "<vfwmmacc_ofp8_fp16_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfwmmacc OFP8 -> BF16: 4 input combinations, acc altfmt = 1.
(define_insn "@pred_vfwmmacc_ofp8_bf16<vfwmmacc_ofp8_bf16_name><mode>"
  [(set (match_operand:VFW_OFP8_BF16_ACC 0 "register_operand" "=&vr")
	(if_then_else:VFW_OFP8_BF16_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VFW_OFP8_BF16_ACC
	    [(match_operand:VFW_OFP8_BF16_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI           2 "register_operand" " vr")
	     (match_operand:RVVM1QI           3 "register_operand" " vr")]
	    VFWMMACC_OFP8_BF16)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_fp_altfmt" "<vfwmmacc_ofp8_bf16_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfwmmacc FP32 -> FP64
(define_insn "@pred_vfwmmacc_f32<mode>"
  [(set (match_operand:VF64_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF64_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF64_ACC
	    [(match_operand:VF64_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1SF  2 "register_operand" " vr")
	     (match_operand:RVVM1SF  3 "register_operand" " vr")]
	    UNSPEC_VFWMMACC_F32)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvfmm - FP matrix multiply-accumulate, W=4 (vfqwmmacc.vv)
;; ---------------------------------------------------------------------------

;; vfqwmmacc FP16 -> FP64
(define_insn "@pred_vfqwmmacc_f16<mode>"
  [(set (match_operand:VF64_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF64_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF64_ACC
	    [(match_operand:VF64_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1HF  2 "register_operand" " vr")
	     (match_operand:RVVM1HF  3 "register_operand" " vr")]
	    UNSPEC_VFQWMMACC_F16)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfqwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfqwmmacc BF16 -> FP64
(define_insn "@pred_vfqwmmacc_bf16<mode>"
  [(set (match_operand:VF64_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF64_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF64_ACC
	    [(match_operand:VF64_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1BF  2 "register_operand" " vr")
	     (match_operand:RVVM1BF  3 "register_operand" " vr")]
	    UNSPEC_VFQWMMACC_BF16)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfqwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfqwmmacc OFP8 (E4M3/E5M2) -> FP32: two variants via int iterator.
(define_insn "@pred_vfqwmmacc_ofp8<vfqwmmacc_ofp8_name><mode>"
  [(set (match_operand:VF32_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF32_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF32_ACC
	    [(match_operand:VF32_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    VFQWMMACC_OFP8)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfqwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_fp_altfmt" "<vfqwmmacc_ofp8_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Zvvfmm - FP matrix multiply-accumulate, W=8 (vf8wmmacc.vv)
;; ---------------------------------------------------------------------------

;; vf8wmmacc OFP8 (E4M3/E5M2) -> FP64: two variants via int iterator.
(define_insn "@pred_vf8wmmacc_ofp8<vf8wmmacc_ofp8_name><mode>"
  [(set (match_operand:VF64_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF64_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF64_ACC
	    [(match_operand:VF64_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    VF8WMMACC_OFP8)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vf8wmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_fp_altfmt" "<vf8wmmacc_ofp8_altfmt>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vf8wmmacc OFP4_E2M1 -> FP32

(define_insn "@pred_vf8wmmacc_f4e2m1<mode>"
  [(set (match_operand:VF32_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF32_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF32_ACC
	    [(match_operand:VF32_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    UNSPEC_VF8WMMACC_F4E2M1)
	  (match_dup 1)))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vf8wmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Microscaled (section 8) FP-input scaled variants
;; ---------------------------------------------------------------------------
;;
;; These are microscaled versions of the section 7 FP mmacc instructions.
;; They use the same assembly mnemonic but with a ",v0.t" suffix to
;; indicate the block-scale vector is present in v0.  The vtype bs bit
;; encodes the block-scale size (BS=16 or BS=32).
;;
;; The block-scale vector (vuint16m1_t) must be in v0.  The expander
;; emits a move into v0 before the instruction; the pattern references
;; v0 implicitly via (use (reg:RVVM1HI V0_REGNUM)).

;; Int iterators for microscaled FP-input scaled variants.
(define_int_iterator VFWMMACC_F16_BS
  [UNSPEC_VFWMMACC_F16_BS32 UNSPEC_VFWMMACC_F16_BS16])

(define_int_attr vfwmmacc_f16_bs_name
  [(UNSPEC_VFWMMACC_F16_BS32 "_bs32")
   (UNSPEC_VFWMMACC_F16_BS16 "_bs16")])

(define_int_attr vfwmmacc_f16_bs_attr
  [(UNSPEC_VFWMMACC_F16_BS32 "bs32")
   (UNSPEC_VFWMMACC_F16_BS16 "bs16")])

(define_int_iterator VFQWMMACC_F8E4M3_BS
  [UNSPEC_VFQWMMACC_F8E4M3_BS32 UNSPEC_VFQWMMACC_F8E4M3_BS16])

(define_int_attr vfqwmmacc_f8e4m3_bs_name
  [(UNSPEC_VFQWMMACC_F8E4M3_BS32 "_bs32")
   (UNSPEC_VFQWMMACC_F8E4M3_BS16 "_bs16")])

(define_int_attr vfqwmmacc_f8e4m3_bs_attr
  [(UNSPEC_VFQWMMACC_F8E4M3_BS32 "bs32")
   (UNSPEC_VFQWMMACC_F8E4M3_BS16 "bs16")])

(define_int_iterator VF8WMMACC_F8E4M3_BS
  [UNSPEC_VF8WMMACC_F8E4M3_BS32 UNSPEC_VF8WMMACC_F8E4M3_BS16])

(define_int_attr vf8wmmacc_f8e4m3_bs_name
  [(UNSPEC_VF8WMMACC_F8E4M3_BS32 "_bs32")
   (UNSPEC_VF8WMMACC_F8E4M3_BS16 "_bs16")])

(define_int_attr vf8wmmacc_f8e4m3_bs_attr
  [(UNSPEC_VF8WMMACC_F8E4M3_BS32 "bs32")
   (UNSPEC_VF8WMMACC_F8E4M3_BS16 "bs16")])

;; Int iterators for microscaled integer-input variants.
(define_int_iterator VFWIMMACC_I8_BS
  [UNSPEC_VFWIMMACC_I8_BS32 UNSPEC_VFWIMMACC_I8_BS16])

(define_int_attr vfwimmacc_i8_bs_name
  [(UNSPEC_VFWIMMACC_I8_BS32 "_bs32")
   (UNSPEC_VFWIMMACC_I8_BS16 "_bs16")])

(define_int_attr vfwimmacc_i8_bs_attr
  [(UNSPEC_VFWIMMACC_I8_BS32 "bs32")
   (UNSPEC_VFWIMMACC_I8_BS16 "bs16")])

(define_int_iterator VFQWIMMACC_I8_BS
  [UNSPEC_VFQWIMMACC_I8_BS32 UNSPEC_VFQWIMMACC_I8_BS16])

(define_int_attr vfqwimmacc_i8_bs_name
  [(UNSPEC_VFQWIMMACC_I8_BS32 "_bs32")
   (UNSPEC_VFQWIMMACC_I8_BS16 "_bs16")])

(define_int_attr vfqwimmacc_i8_bs_attr
  [(UNSPEC_VFQWIMMACC_I8_BS32 "bs32")
   (UNSPEC_VFQWIMMACC_I8_BS16 "bs16")])

(define_int_iterator VF8WIMMACC_I8_BS
  [UNSPEC_VF8WIMMACC_I8_BS32 UNSPEC_VF8WIMMACC_I8_BS16])

(define_int_attr vf8wimmacc_i8_bs_name
  [(UNSPEC_VF8WIMMACC_I8_BS32 "_bs32")
   (UNSPEC_VF8WIMMACC_I8_BS16 "_bs16")])

(define_int_attr vf8wimmacc_i8_bs_attr
  [(UNSPEC_VF8WIMMACC_I8_BS32 "bs32")
   (UNSPEC_VF8WIMMACC_I8_BS16 "bs16")])

;; FP16 accumulator modes at LMUL >= 1 (for vfwimmacc Int8->FP16).
(define_mode_iterator VF16_ACC
  [(RVVM1HF "TARGET_ZVFH") (RVVM2HF "TARGET_ZVFH")
   (RVVM4HF "TARGET_ZVFH") (RVVM8HF "TARGET_ZVFH")])

;; vfwmmacc_scaled FP16 -> FP32, BS=32/BS=16
(define_insn "@pred_vfwmmacc_scaled_f16<vfwmmacc_f16_bs_name><mode>"
  [(set (match_operand:VF32_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF32_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF32_ACC
	    [(match_operand:VF32_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1HF  2 "register_operand" " vr")
	     (match_operand:RVVM1HF  3 "register_operand" " vr")]
	    VFWMMACC_F16_BS)
	  (match_dup 1)))
   (use (reg:RVVM1HI V0_REGNUM))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfwmmacc.vv\t%0,%2,%3,v0.t"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_bs" "<vfwmmacc_f16_bs_attr>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfqwmmacc_scaled OFP8_E4M3 -> FP32, BS=32/BS=16
(define_insn "@pred_vfqwmmacc_scaled_f8e4m3<vfqwmmacc_f8e4m3_bs_name><mode>"
  [(set (match_operand:VF32_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF32_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF32_ACC
	    [(match_operand:VF32_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    VFQWMMACC_F8E4M3_BS)
	  (match_dup 1)))
   (use (reg:RVVM1HI V0_REGNUM))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfqwmmacc.vv\t%0,%2,%3,v0.t"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_bs" "<vfqwmmacc_f8e4m3_bs_attr>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vf8wmmacc_scaled OFP8_E4M3 -> FP64, BS=32/BS=16
(define_insn "@pred_vf8wmmacc_scaled_f8e4m3<vf8wmmacc_f8e4m3_bs_name><mode>"
  [(set (match_operand:VF64_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF64_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF64_ACC
	    [(match_operand:VF64_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    VF8WMMACC_F8E4M3_BS)
	  (match_dup 1)))
   (use (reg:RVVM1HI V0_REGNUM))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vf8wmmacc.vv\t%0,%2,%3,v0.t"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_bs" "<vf8wmmacc_f8e4m3_bs_attr>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; ---------------------------------------------------------------------------
;; Microscaled (section 8) integer-input variants (NEW instructions)
;; ---------------------------------------------------------------------------

;; vfwimmacc.vv Int8 -> FP16, BS=32/BS=16
(define_insn "@pred_vfwimmacc_i8<vfwimmacc_i8_bs_name><mode>"
  [(set (match_operand:VF16_ACC 0 "register_operand"              "=&vr")
	(if_then_else:VF16_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF16_ACC
	    [(match_operand:VF16_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    VFWIMMACC_I8_BS)
	  (match_dup 1)))
   (use (reg:RVVM1HI V0_REGNUM))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfwimmacc.vv\t%0,%2,%3,v0.t"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_bs" "<vfwimmacc_i8_bs_attr>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vfqwimmacc.vv Int8 -> FP32, BS=32/BS=16
(define_insn "@pred_vfqwimmacc_i8<vfqwimmacc_i8_bs_name><mode>"
  [(set (match_operand:VF32_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF32_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF32_ACC
	    [(match_operand:VF32_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    VFQWIMMACC_I8_BS)
	  (match_dup 1)))
   (use (reg:RVVM1HI V0_REGNUM))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vfqwimmacc.vv\t%0,%2,%3,v0.t"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_bs" "<vfqwimmacc_i8_bs_attr>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])

;; vf8wimmacc.vv Int8 -> FP64, BS=32/BS=16
(define_insn "@pred_vf8wimmacc_i8<vf8wimmacc_i8_bs_name><mode>"
  [(set (match_operand:VF64_ACC 0 "register_operand" "=&vr")
	(if_then_else:VF64_ACC
	  (unspec:<VM>
	    [(match_operand 4 "vector_length_operand" " rK")
	     (match_operand 5 "const_int_operand"     "  i")
	     (match_operand 6 "const_int_operand"     "  i")
	     (reg:SI VL_REGNUM)
	     (reg:SI VTYPE_REGNUM)] UNSPEC_VPREDICATE)
	  (unspec:VF64_ACC
	    [(match_operand:VF64_ACC 1 "register_operand" "  0")
	     (match_operand:RVVM1QI  2 "register_operand" " vr")
	     (match_operand:RVVM1QI  3 "register_operand" " vr")]
	    VF8WIMMACC_I8_BS)
	  (match_dup 1)))
   (use (reg:RVVM1HI V0_REGNUM))
   (use (match_operand 7 "vtype_op_operand" "rJ"))]
  "TARGET_ZVVFMM"
  "vf8wimmacc.vv\t%0,%2,%3,v0.t"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "ime_bs" "<vf8wimmacc_i8_bs_attr>")
   (set_attr "ime_vtype_op_idx" "7")
   (set_attr "mode" "<MODE>")])
