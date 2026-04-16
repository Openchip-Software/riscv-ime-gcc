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

;; Integer modes at LMUL >= 1 for the tile load/store patterns.  The
;; IME spec admits LMUL in {1, 2, 4, 8}; fractional LMULs are not
;; expressible in the tile register-group geometry and are excluded.
(define_mode_iterator VI_LMUL1 [RVVM1QI RVVM2QI RVVM4QI RVVM8QI
				RVVM1HI RVVM2HI RVVM4HI RVVM8HI
				RVVM1SI RVVM2SI RVVM4SI RVVM8SI
				RVVM1DI RVVM2DI RVVM4DI RVVM8DI])

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

;; ---------------------------------------------------------------------------
;; Zvvmm - Vector integer matrix multiply-accumulate, W=4 (vqwmmacc.vv)
;; ---------------------------------------------------------------------------
;;
;; vqwmmacc.vv vd, vs1, vs2 -- accumulator at SEW, inputs at SEW/4 and
;; LMUL=1.  Four sign combinations share the same assembly mnemonic;
;; altfmt_A / altfmt_B in vtype encode the per-side signedness.
(define_insn "@pred_vqwmmacc<vqwmmacc_su_name><mode>"
  [(set (match_operand:VQWMMACC_ACC 0 "register_operand"        "=vr")
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
	  (match_dup 1)))]
  "TARGET_ZVVMM"
  "vqwmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<vqwmmacc_altfmt>")
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
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))]
  "TARGET_ZVVMTLS"
  "vmtl.v\t%0,%3,%4%p1"
  [(set_attr "type" "vlds")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
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
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))]
  "TARGET_ZVVMTLS"
  "vmtl.v\t%0,%3,%4,<vmtl_L_imm>%p1"
  [(set_attr "type" "vlds")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
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
	  UNSPEC_VMTS))]
  "TARGET_ZVVMTLS"
  "vmts.v\t%3,%0,%2%p1"
  [(set_attr "type" "vsts")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
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
	  VMTS_L))]
  "TARGET_ZVVMTLS"
  "vmts.v\t%3,%0,%2,<vmts_L_imm>%p1"
  [(set_attr "type" "vsts")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
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
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))]
  "TARGET_ZVVMTTLS"
  "vmttl.v\t%0,%3,%4%p1"
  [(set_attr "type" "vlds")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
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
	  (match_operand:VI_LMUL1 2 "vector_merge_operand"   "   vu")))]
  "TARGET_ZVVMTTLS"
  "vmttl.v\t%0,%3,%4,<vmttl_L_imm>%p1"
  [(set_attr "type" "vlds")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
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
	  UNSPEC_VMTTS))]
  "TARGET_ZVVMTTLS"
  "vmtts.v\t%3,%0,%2%p1"
  [(set_attr "type" "vsts")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
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
	  VMTTS_L))]
  "TARGET_ZVVMTTLS"
  "vmtts.v\t%3,%0,%2,<vmtts_L_imm>%p1"
  [(set_attr "type" "vsts")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "ss")
   (set_attr "mode" "<MODE>")
   (set (attr "avl_type_idx") (const_int 5))])

(define_insn "@pred_v8wmmacc<v8wmmacc_su_name><mode>"
  [(set (match_operand:V8WMMACC_ACC 0 "register_operand"        "=vr")
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
	  (match_dup 1)))]
  "TARGET_ZVVMM"
  "v8wmmacc.vv\t%0,%2,%3"
  [(set_attr "type" "vmmacc")
   (set_attr "requires_ime_vtype" "yes")
   (set_attr "ime_altfmt" "<v8wmmacc_altfmt>")
   (set_attr "mode" "<MODE>")])
