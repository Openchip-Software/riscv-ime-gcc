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
  UNSPEC_VWMMACC_S   ;; Zvvmm vwmmacc.vv, signed*signed    (W=2, altfmt_A=0, altfmt_B=0)
  UNSPEC_VWMMACC_U   ;; Zvvmm vwmmacc.vv, unsigned*unsigned (W=2, altfmt_A=1, altfmt_B=1)
  UNSPEC_VWMMACC_SU  ;; Zvvmm vwmmacc.vv, signed*unsigned  (W=2, altfmt_A=0, altfmt_B=1)
  UNSPEC_VWMMACC_US  ;; Zvvmm vwmmacc.vv, unsigned*signed  (W=2, altfmt_A=1, altfmt_B=0)

  ;; Register-form vsetvl used to establish the full vtype value for IME
  ;; instructions (lambda, altfmt_A, altfmt_B, altfmt, bs in addition to the
  ;; standard SEW/LMUL/ta/ma).  A plain vsetvli/vsetivli cannot encode these
  ;; upper vtype bits; they require a register source containing the full
  ;; vtype value.
  UNSPEC_VSETVL_IME
])

;; Signedness encoding for the two integer-input operands of IME
;; multiply-accumulate instructions, as it maps to the vtype altfmt_A /
;; altfmt_B bits that the vsetvl pass establishes.  For now, only the
;; same-sign cases (ss, uu) are implemented; mixed-sign (su, us) lands
;; with the widening variants.
(define_attr "ime_altfmt" "ss,uu,su,us"
  (const_string "ss"))

(define_int_iterator VMMACC_SU [UNSPEC_VMMACC_S UNSPEC_VMMACC_U])

(define_int_attr vmmacc_su_name [(UNSPEC_VMMACC_S "_s")
				 (UNSPEC_VMMACC_U "_u")])

(define_int_attr vmmacc_altfmt [(UNSPEC_VMMACC_S "ss")
				(UNSPEC_VMMACC_U "uu")])

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
;; variant; _lm{N} variants for LMUL > 1 will get their own patterns.
;;
;; Signed (UNSPEC_VMMACC_S) and unsigned (UNSPEC_VMMACC_U) emit the same
;; vmmacc.vv instruction; the distinction lives entirely in the vtype
;; altfmt_A / altfmt_B bits that the vsetvl pass establishes from the
;; ime_altfmt attribute.
;;
;; No mask, no merge operand: IME multiply-accumulate instructions do not
;; support element-level masking or tail policies in the standard RVV sense.
(define_insn "@pred_vmmacc<vmmacc_su_name><mode>"
  [(set (match_operand:VI 0 "register_operand"              "=vr")
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
	  (match_dup 1)))]
  "TARGET_ZVVMM"
  "vmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vmmacc_altfmt>")
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
  [(set (match_operand:VWMMACC_ACC 0 "register_operand"        "=vr")
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
	  (match_dup 1)))]
  "TARGET_ZVVMM"
  "vwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vwmmacc_altfmt>")
   (set_attr "mode" "<MODE>")])
