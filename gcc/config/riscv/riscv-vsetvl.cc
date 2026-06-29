/* VSETVL pass for RISC-V 'V' Extension for GNU compiler.
   Copyright (C) 2022-2026 Free Software Foundation, Inc.
   Contributed by Juzhe Zhong (juzhe.zhong@rivai.ai), RiVAI Technologies Ltd.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or(at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* The values of the vl and vtype registers will affect the behavior of RVV
   insns. That is, when we need to execute an RVV instruction, we need to set
   the correct vl and vtype values by executing the vsetvl instruction before.
   Executing the fewest number of vsetvl instructions while keeping the behavior
   the same is the problem this pass is trying to solve. This vsetvl pass is
   divided into 5 phases:

     - Phase 1 (fuse local vsetvl infos): traverses each Basic Block, parses
       each instruction in it that affects vl and vtype state and generates an
       array of vsetvl_info objects. Then traverse the vsetvl_info array from
       front to back and perform fusion according to the fusion rules. The fused
       vsetvl infos are stored in the vsetvl_block_info object's `infos` field.

     - Phase 2 (earliest fuse global vsetvl infos): The header_info and
       footer_info of vsetvl_block_info are used as expressions, and the
       earliest of each expression is computed. Based on the earliest
       information, try to lift up the corresponding vsetvl info to the src
       basic block of the edge (mainly to reduce the total number of vsetvl
       instructions, this uplift will cause some execution paths to execute
       vsetvl instructions that shouldn't be there).

     - Phase 3 (pre global vsetvl info): The header_info and footer_info of
       vsetvl_block_info are used as expressions, and the LCM algorithm is used
       to compute the header_info that needs to be deleted and the one that
       needs to be inserted in some edges.

     - Phase 4 (emit vsetvl insns) : Based on the fusion result of Phase 1 and
       the deletion and insertion information of Phase 3, the mandatory vsetvl
       instruction insertion, modification and deletion are performed.

     - Phase 5 (cleanup): Clean up the avl operand in the RVV operator
       instruction and cleanup the unused dest operand of the vsetvl insn.

     After the Phase 1 a virtual CFG of vsetvl_info is generated. The virtual
     basic block is represented by vsetvl_block_info, and the virtual vsetvl
     statements inside are represented by vsetvl_info. The later phases 2 and 3
     are constantly modifying and adjusting this virtual CFG. Phase 4 performs
     insertion, modification and deletion of vsetvl instructions based on the
     optimized virtual CFG. The Phase 1, 2 and 3 do not involve modifications to
     the RTL.
*/

#define IN_TARGET_CODE 1
#define INCLUDE_ALGORITHM
#define INCLUDE_FUNCTIONAL
#define INCLUDE_ARRAY

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "backend.h"
#include "rtl.h"
#include "target.h"
#include "tree-pass.h"
#include "df.h"
#include "rtl-ssa.h"
#include "cfgcleanup.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "insn-opinit.h"
#include "tm-constrs.h"
#include "cfgrtl.h"
#include "cfganal.h"
#include "lcm.h"
#include "predict.h"
#include "profile-count.h"
#include "gcse.h"
#include "cfgloop.h"
#include "diagnostic-core.h"

using namespace rtl_ssa;
using namespace riscv_vector;

/* Set the bitmap DST to the union of SRC of predecessors of
   basic block B.
   It's a bit different from bitmap_union_of_preds in cfganal.cc. This function
   takes into account the case where pred is ENTRY basic block. The main reason
   for this difference is to make it easier to insert some special value into
   the ENTRY base block. For example, vsetvl_info with a status of UNKNOWN.  */
static void
bitmap_union_of_preds_with_entry (sbitmap dst, sbitmap *src, basic_block b)
{
  /* Handle case with no predecessors (including ENTRY block).  */
  if (EDGE_COUNT (b->preds) == 0)
    {
      bitmap_clear (dst);
      return;
    }

  edge e;
  edge_iterator ei;
  /* Union remaining predecessors' bitmaps.  */
  FOR_EACH_EDGE (e, ei, b->preds)
    {
      /* Initialize with first predecessor's bitmap.  */
      if (ei.index == 0)
	{
	  bitmap_copy (dst, src[e->src->index]);
	  continue;
	}

      /* Perform bitmap OR operation element-wise.  */
      bitmap_ior (dst, dst, src[e->src->index]);
    }
}

/* Compute the reaching definition in and out based on the gen and KILL
   information's in each Base Blocks.
   This function references the compute_available implementation in lcm.cc  */
static void
compute_reaching_definition (sbitmap *gen, sbitmap *kill, sbitmap *in,
			    sbitmap *out)
{
  edge e;
  basic_block *worklist, *qin, *qout, *qend, bb;
  unsigned int qlen;
  edge_iterator ei;

  /* Allocate a worklist array/queue.  Entries are only added to the
     list if they were not already on the list.  So the size is
     bounded by the number of basic blocks.  */
  qin = qout = worklist
    = XNEWVEC (basic_block, n_basic_blocks_for_fn (cfun) - NUM_FIXED_BLOCKS);

  /* Put every block on the worklist; this is necessary because of the
     optimistic initialization of AVOUT above.  Use reverse postorder
     to make the forward dataflow problem require less iterations.  */
  int *rpo = XNEWVEC (int, n_basic_blocks_for_fn (cfun) - NUM_FIXED_BLOCKS);
  int n = pre_and_rev_post_order_compute_fn (cfun, NULL, rpo, false);
  for (int i = 0; i < n; ++i)
    {
      bb = BASIC_BLOCK_FOR_FN (cfun, rpo[i]);
      *qin++ = bb;
      bb->aux = bb;
    }
  free (rpo);

  qin = worklist;
  qend = &worklist[n_basic_blocks_for_fn (cfun) - NUM_FIXED_BLOCKS];
  qlen = n_basic_blocks_for_fn (cfun) - NUM_FIXED_BLOCKS;

  /* Mark blocks which are successors of the entry block so that we
     can easily identify them below.  */
  FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR_FOR_FN (cfun)->succs)
    e->dest->aux = ENTRY_BLOCK_PTR_FOR_FN (cfun);

  /* Iterate until the worklist is empty.  */
  while (qlen)
    {
      /* Take the first entry off the worklist.  */
      bb = *qout++;
      qlen--;

      if (qout >= qend)
	qout = worklist;

      /* Do not clear the aux field for blocks which are successors of the
	 ENTRY block.  That way we never add then to the worklist again.  */
      if (bb->aux != ENTRY_BLOCK_PTR_FOR_FN (cfun))
	bb->aux = NULL;

      bitmap_union_of_preds_with_entry (in[bb->index], out, bb);

      if (bitmap_ior_and_compl (out[bb->index], gen[bb->index], in[bb->index],
				kill[bb->index]))
	/* If the out state of this block changed, then we need
	   to add the successors of this block to the worklist
	   if they are not already on the worklist.  */
	FOR_EACH_EDGE (e, ei, bb->succs)
	  if (!e->dest->aux && e->dest != EXIT_BLOCK_PTR_FOR_FN (cfun))
	    {
	      *qin++ = e->dest;
	      e->dest->aux = e;
	      qlen++;

	      if (qin >= qend)
		qin = worklist;
	    }
    }

  clear_aux_for_edges ();
  clear_aux_for_blocks ();
  free (worklist);
}

/* Classification of vsetvl instruction.  */
enum vsetvl_type
{
  VSETVL_NORMAL,
  VSETVL_VTYPE_CHANGE_ONLY,
  VSETVL_DISCARD_RESULT,
  NUM_VSETVL_TYPE
};

enum emit_type
{
  /* emit_insn directly.  */
  EMIT_DIRECT,
  EMIT_BEFORE,
  EMIT_AFTER,
};

static const int MAX_LMUL = 8;

/* dump helper functions */
static const char *
vlmul_to_str (vlmul_type vlmul)
{
  switch (vlmul)
    {
    case LMUL_1:
      return "m1";
    case LMUL_2:
      return "m2";
    case LMUL_4:
      return "m4";
    case LMUL_8:
      return "m8";
    case LMUL_16:
      return "m16";
    case LMUL_RESERVED:
      return "INVALID LMUL";
    case LMUL_F8:
      return "mf8";
    case LMUL_F4:
      return "mf4";
    case LMUL_F2:
      return "mf2";

    default:
      gcc_unreachable ();
    }
}

static const char *
policy_to_str (bool agnostic_p)
{
  return agnostic_p ? "agnostic" : "undisturbed";
}

/* Return true if it is an RVV instruction depends on VTYPE global
   status register.  */
bool
has_vtype_op (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0 && get_attr_has_vtype_op (rinsn);
}

/* Return true if the instruction ignores VLMUL field of VTYPE.  */
static bool
ignore_vlmul_insn_p (rtx_insn *rinsn)
{
  return get_attr_type (rinsn) == TYPE_VIMOVVX
	 || get_attr_type (rinsn) == TYPE_VFMOVVF
	 || get_attr_type (rinsn) == TYPE_VIMOVXV
	 || get_attr_type (rinsn) == TYPE_VFMOVFV;
}

/* Return true if the instruction is scalar move instruction.  */
static bool
scalar_move_insn_p (rtx_insn *rinsn)
{
  return get_attr_type (rinsn) == TYPE_VIMOVXV
	 || get_attr_type (rinsn) == TYPE_VFMOVFV;
}

/* Return true if the instruction is fault first load instruction.  */
static bool
fault_first_load_p (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0
	 && (get_attr_type (rinsn) == TYPE_VLDFF
	     || get_attr_type (rinsn) == TYPE_VLSEGDFF);
}

/* Return the VL output register from a fault-only-first load with VL
   output (pred_fault_load_set_vl pattern) if RINSN is such an insn
   or NULL_RTX otherwise.
   The pattern has: (set vl_output (unspec:P [(reg:SI VL_REGNUM)]
					     UNSPEC_READ_VL))  */
static rtx
get_fof_set_vl_reg (rtx_insn *rinsn)
{
  if (!fault_first_load_p (rinsn))
    return NULL_RTX;

  rtx pat = PATTERN (rinsn);
  if (GET_CODE (pat) != PARALLEL)
    return NULL_RTX;

  if (XVECLEN (pat, 0) != 3)
    return NULL_RTX;

  rtx sub = XVECEXP (pat, 0, 2);
  if (GET_CODE (sub) == SET
      && GET_CODE (SET_SRC (sub)) == UNSPEC
      && XINT (SET_SRC (sub), 1) == UNSPEC_READ_VL)
    return SET_DEST (sub);

  return NULL_RTX;
}

/* Initialize RTL SSA and related infrastructure for vsetvl analysis.  */
static void
init_rtl_ssa ()
{
  calculate_dominance_info (CDI_DOMINATORS);
  loop_optimizer_init (AVOID_CFG_MODIFICATIONS);
  connect_infinite_loops_to_exit ();
  df_analyze ();
  crtl->ssa = new function_info (cfun);
}

/* Finalize RTL SSA and cleanup.  */
static void
finish_rtl_ssa ()
{
  free_dominance_info (CDI_DOMINATORS);
  loop_optimizer_finalize ();
  if (crtl->ssa->perform_pending_updates ())
    cleanup_cfg (0);
  delete crtl->ssa;
  crtl->ssa = nullptr;
}

/* Emit read_vl instructions after fault-only-first loads that have
   a VL output register.
   This needs to happen last, i.e. when we made the VL dataflow
   explicit by inserting vsetvls.  */

static void
emit_fof_read_vls ()
{
  basic_block bb;
  rtx_insn *rinsn;

  FOR_EACH_BB_FN (bb, cfun)
    FOR_BB_INSNS (bb, rinsn)
      {
	if (!NONDEBUG_INSN_P (rinsn))
	  continue;

	rtx vl_dest = get_fof_set_vl_reg (rinsn);
	if (!vl_dest)
	  continue;

	if (dump_file)
	  fprintf (dump_file,
		   "  Inserting read_vl after FoF insn %d into r%d\n",
		   INSN_UID (rinsn), REGNO (vl_dest));

	rtx read_vl_pat = gen_read_vl (Pmode, vl_dest);
	emit_insn_after (read_vl_pat, rinsn);
      }
}

/* Return true if the instruction is read vl instruction.  */
static bool
read_vl_insn_p (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0 && get_attr_type (rinsn) == TYPE_RDVL;
}

/* Return true if it is a vsetvl instruction.  */
static bool
vector_config_insn_p (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0 && get_attr_type (rinsn) == TYPE_VSETVL;
}

/* Return true if it is vsetvldi or vsetvlsi.  */
bool
vsetvl_insn_p (rtx_insn *rinsn)
{
  if (!rinsn || !vector_config_insn_p (rinsn))
    return false;
  return (INSN_CODE (rinsn) == CODE_FOR_vsetvldi
	  || INSN_CODE (rinsn) == CODE_FOR_vsetvlsi);
}

/* Return true if RINSN is `@riscv_vsetvl_preserve_lambda<mode>'.

   Semantically this is BOTH a vtype source (writes vtype with known
   SEW/LMUL/ta/ma encoded in its const_int operand 2) AND a vtype sink
   in the sense that its run-time value depends on the live vtype.lambda
   field -- any plain vsetvli placed upstream of it would WARL-clobber
   that lambda and corrupt the merge.

   The pass's "modify-or-skip" path is gated by `vsetvl_insn_p' (which
   hardcodes the two CODE_FOR_vsetvl<di,si> values).  preserve_lambda
   is not such a plain vsetvl -- changing it via `change_vsetvl_insn'
   would replace it with `gen_vsetvl(...)' which loses the lambda
   merge, so we never want to take the modify branch.  Likewise we
   must never take the "insert vsetvl before" branch for preserve_lambda
   in `emit_vsetvl': that would lift a plain vsetvli above the csrr
   that reads the live lambda.  This predicate gates those sites.  */
static bool
is_preserve_lambda_p (rtx_insn *rinsn)
{
  if (!rinsn)
    return false;
  int code = recog_memoized (rinsn);
  return (code == CODE_FOR_riscv_vsetvl_preserve_lambdadi
	  || code == CODE_FOR_riscv_vsetvl_preserve_lambdasi);
}

/* Return true if it is the bogus vsetvl_pre instruction:

   (define_insn "@vlmax_avl<mode>"
     [(set (match_operand:P 0 "register_operand" "=r")
	(unspec:P [(match_operand:P 1 "const_int_operand" "i")] UNSPEC_VLMAX))]
     "TARGET_VECTOR"
     ""
     [(set_attr "type" "vsetvl_pre")])

   As described above, it's the bogus instruction which doesn't any assembler
   and should be removed eventually.  It's used for occupying a scalar register
   for VLMAX avl RVV instruction before register allocation.

   Before RA:

   ...
   vsetvl_pre (set r136)
   vadd.vv (use r136 with VLMAX avl)
   ...

   After RA:

   ...
   vsetvl_pre (set a5)
   vadd.vv (use r136 with VLMAX avl)
   ...

   VSETVL PASS:

   ...
   vsetvl_pre (set a5) ---> removed.
   vsetvl a5,zero,...  ---> Inserted.
   vadd.vv
   ...
*/
static bool
vsetvl_pre_insn_p (rtx_insn *rinsn)
{
  return recog_memoized (rinsn) >= 0
	 && get_attr_type (rinsn) == TYPE_VSETVL_PRE;
}

/* Return true if it is vsetvl zero, rs1.  */
static bool
vsetvl_discard_result_insn_p (rtx_insn *rinsn)
{
  if (!vector_config_insn_p (rinsn))
    return false;
  return (INSN_CODE (rinsn) == CODE_FOR_vsetvl_discard_resultdi
	  || INSN_CODE (rinsn) == CODE_FOR_vsetvl_discard_resultsi);
}

static bool
real_insn_and_same_bb_p (const insn_info *insn, const bb_info *bb)
{
  return insn != nullptr && insn->is_real () && insn->bb () == bb;
}

/* Helper function to get VL operand for VLMAX insn.  */
static rtx
get_vl (rtx_insn *rinsn)
{
  if (has_vl_op (rinsn))
    {
      extract_insn_cached (rinsn);
      return recog_data.operand[get_attr_vl_op_idx (rinsn)];
    }
  return SET_DEST (XVECEXP (PATTERN (rinsn), 0, 0));
}

/* Helper function to get AVL operand.  */
rtx
get_avl (rtx_insn *rinsn)
{
  if (vsetvl_insn_p (rinsn) || vsetvl_discard_result_insn_p (rinsn))
    return XVECEXP (SET_SRC (XVECEXP (PATTERN (rinsn), 0, 0)), 0, 0);

  if (!has_vl_op (rinsn))
    return NULL_RTX;
  if (vlmax_avl_type_p (rinsn))
    return RVV_VLMAX;
  extract_insn_cached (rinsn);
  return recog_data.operand[get_attr_vl_op_idx (rinsn)];
}

/* Get default mask policy.  */
static bool
get_default_ma ()
{
  /* For the instruction that doesn't require MA, we still need a default value
     to emit vsetvl. We pick up the default value according to prefer policy. */
  return (bool) (get_prefer_mask_policy () & 0x1
		 || (get_prefer_mask_policy () >> 1 & 0x1));
}

/* Helper function to get MA operand.  */
bool
mask_agnostic_p (rtx_insn *rinsn)
{
  /* If it doesn't have MA, we return agnostic by default.  */
  extract_insn_cached (rinsn);
  int ma = get_attr_ma (rinsn);
  return ma == INVALID_ATTRIBUTE ? get_default_ma () : IS_AGNOSTIC (ma);
}

/* Return true if FN has a vector instruction that use VL/VTYPE.  */
static bool
has_vector_insn (function *fn)
{
  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, fn)
    FOR_BB_INSNS (cfg_bb, rinsn)
      {
	if (!NONDEBUG_INSN_P (rinsn))
	  continue;
	if (has_vtype_op (rinsn))
	  return true;
	/* `__riscv_vsetlambda' installs write vtype directly (no
	   vtype_op attribute), but the vsetvl pass still needs to
	   run for them so the lambda dataflow can perform DCE.  */
	int code = INSN_CODE (rinsn);
	if (code == CODE_FOR_riscv_vsetlambda_installdi
	    || code == CODE_FOR_riscv_vsetlambda_installsi)
	  return true;
      }
  return false;
}

static vlmul_type
calculate_vlmul (unsigned int sew, unsigned int ratio)
{
  const vlmul_type ALL_LMUL[]
    = {LMUL_1, LMUL_2, LMUL_4, LMUL_8, LMUL_F8, LMUL_F4, LMUL_F2};
  for (const vlmul_type vlmul : ALL_LMUL)
    if (calculate_ratio (sew, vlmul) == ratio)
      return vlmul;
  return LMUL_RESERVED;
}

/* Decode a static vtype const_int (the form built by
   `compute_static_vtype_without_lambda' and consumed by
   `@vsetvl_ime<mode>' / `@riscv_vsetvl_preserve_lambda<mode>') into
   the SEW/LMUL/ta/ma fields the rest of the pass works with.  The
   lambda field is *not* decoded -- the caller decides whether that
   came from the const (vsetvl_ime) or from the runtime csrr-and-merge
   (preserve_lambda).  Returns false if VTYPE_CONST encodes a SEW
   value out of range.  */

static bool
decode_static_vtype_const (HOST_WIDE_INT vtype_const,
			   uint8_t *sew_out, vlmul_type *vlmul_out,
			   bool *ta_out, bool *ma_out)
{
  unsigned vlmul_enc = vtype_const & 0x7;
  unsigned sew_enc = (vtype_const >> 3) & 0x7;
  if (sew_enc > 3)
    return false;  /* only e8/e16/e32/e64 are legal.  */
  *sew_out = (uint8_t) (8u << sew_enc);
  *vlmul_out = (vlmul_type) vlmul_enc;
  *ta_out = (vtype_const >> 6) & 1;
  *ma_out = (vtype_const >> 7) & 1;
  return true;
}

/* Pack the consumer-side IME demand (the fields of `ime_vtype_req' that
   are NOT recoverable from the static vtype bits) into a CONST_INT so it
   can ride along as an operand of the `@vsetvl_ime<mode>' / `@vsetvl_ime_reg<mode>'
   patterns.  This lets `vsetvl_info::parse_insn' recover the full IME
   demand for the emitted vsetvl, instead of marking it UNKNOWN.

   Layout (HOST_WIDE_INT):
     [ 7: 0] sew         (8/16/32/64)
     [11: 8] mul_c       (vlmul_type)
     [15:12] lmul        (input lmul, vlmul_type)
     [19:16] widen_w     (1/2/4/8)
     [   20] altfmt_a
     [   21] altfmt_b
     [   22] altfmt
     [   23] bs
     [31:24] lambda_log2 + 128  (-1..6 stored offset)  */
static HOST_WIDE_INT
pack_ime_req_fields (uint8_t sew, uint8_t mul_c, uint8_t lmul,
		     uint8_t widen_w,
		     bool altfmt_a, bool altfmt_b,
		     bool altfmt, bool bs,
		     int8_t lambda_log2)
{
  HOST_WIDE_INT p = 0;
  p |= (HOST_WIDE_INT) sew & 0xff;
  p |= ((HOST_WIDE_INT) mul_c & 0xf) << 8;
  p |= ((HOST_WIDE_INT) lmul & 0xf) << 12;
  p |= ((HOST_WIDE_INT) widen_w & 0xf) << 16;
  p |= ((HOST_WIDE_INT) (altfmt_a ? 1 : 0)) << 20;
  p |= ((HOST_WIDE_INT) (altfmt_b ? 1 : 0)) << 21;
  p |= ((HOST_WIDE_INT) (altfmt ? 1 : 0)) << 22;
  p |= ((HOST_WIDE_INT) (bs ? 1 : 0)) << 23;
  p |= ((HOST_WIDE_INT) ((int) lambda_log2 + 128) & 0xff) << 24;
  return p;
}

/* Forward-declared for use from riscv-v.cc (emit_ime_runtime_vtype_setup).
   Declared in riscv-v.h inside the riscv_vector namespace; provide the
   matching definition here.  */
namespace riscv_vector
{
HOST_WIDE_INT
riscv_pack_ime_req (uint8_t sew, uint8_t mul_c, uint8_t lmul,
		    uint8_t widen_w,
		    bool altfmt_a, bool altfmt_b,
		    bool altfmt, bool bs,
		    int8_t lambda_log2)
{
  return pack_ime_req_fields (sew, mul_c, lmul, widen_w,
			      altfmt_a, altfmt_b, altfmt, bs,
			      lambda_log2);
}
} // namespace riscv_vector

/* Get the currently supported maximum sew used in the int rvv instructions. */
static uint8_t
get_max_int_sew ()
{
  if (TARGET_VECTOR_ELEN_64)
    return 64;
  else if (TARGET_VECTOR_ELEN_32)
    return 32;
  gcc_unreachable ();
}

/* Get the currently supported maximum sew used in the float rvv instructions.
 */
static uint8_t
get_max_float_sew ()
{
  if (TARGET_VECTOR_ELEN_FP_64)
    return 64;
  else if (TARGET_VECTOR_ELEN_FP_32)
    return 32;
  else if (TARGET_VECTOR_ELEN_FP_16)
    return 16;
  gcc_unreachable ();
}

enum def_type
{
  REAL_SET = 1 << 0,
  PHI_SET = 1 << 1,
  BB_HEAD_SET = 1 << 2,
  BB_END_SET = 1 << 3,
  /* ??? TODO: In RTL_SSA framework, we have REAL_SET,
     PHI_SET, BB_HEAD_SET, BB_END_SET and
     CLOBBER_DEF def_info types. Currently,
     we conservatively do not optimize clobber
     def since we don't see the case that we
     need to optimize it.  */
  CLOBBER_DEF = 1 << 4
};

static bool
insn_should_be_added_p (const insn_info *insn, unsigned int types)
{
  if (insn->is_real () && (types & REAL_SET))
    return true;
  if (insn->is_phi () && (types & PHI_SET))
    return true;
  if (insn->is_bb_head () && (types & BB_HEAD_SET))
    return true;
  if (insn->is_bb_end () && (types & BB_END_SET))
    return true;
  return false;
}

static const hash_set<use_info *>
get_all_real_uses (insn_info *insn, unsigned regno)
{
  gcc_assert (insn->is_real ());

  hash_set<use_info *> uses;
  auto_vec<phi_info *> work_list;
  hash_set<phi_info *> visited_list;

  for (def_info *def : insn->defs ())
    {
      if (!def->is_reg () || def->regno () != regno)
	continue;
      set_info *set = safe_dyn_cast<set_info *> (def);
      if (!set)
	continue;
      for (use_info *use : set->nondebug_insn_uses ())
	if (use->insn ()->is_real ())
	  uses.add (use);
      for (use_info *use : set->phi_uses ())
	work_list.safe_push (use->phi ());
    }

  while (!work_list.is_empty ())
    {
      phi_info *phi = work_list.pop ();
      visited_list.add (phi);

      for (use_info *use : phi->nondebug_insn_uses ())
	if (use->insn ()->is_real ())
	  uses.add (use);
      for (use_info *use : phi->phi_uses ())
	if (!visited_list.contains (use->phi ()))
	  work_list.safe_push (use->phi ());
    }
  return uses;
}

/* Recursively find all define instructions. The kind of instruction is
   specified by the DEF_TYPE.  */
static hash_set<set_info *>
get_all_sets (phi_info *phi, unsigned int types)
{
  hash_set<set_info *> insns;
  auto_vec<phi_info *> work_list;
  hash_set<phi_info *> visited_list;
  if (!phi)
    return hash_set<set_info *> ();
  work_list.safe_push (phi);

  while (!work_list.is_empty ())
    {
      phi_info *phi = work_list.pop ();
      visited_list.add (phi);
      for (use_info *use : phi->inputs ())
	{
	  def_info *def = use->def ();
	  set_info *set = safe_dyn_cast<set_info *> (def);
	  if (!set)
	    return hash_set<set_info *> ();

	  gcc_assert (!set->insn ()->is_debug_insn ());

	  if (insn_should_be_added_p (set->insn (), types))
	    insns.add (set);
	  if (set->insn ()->is_phi ())
	    {
	      phi_info *new_phi = as_a<phi_info *> (set);
	      if (!visited_list.contains (new_phi))
		work_list.safe_push (new_phi);
	    }
	}
    }
  return insns;
}

static hash_set<set_info *>
get_all_sets (set_info *set, bool /* get_real_inst */ real_p,
	      bool /*get_phi*/ phi_p, bool /* get_function_parameter*/ param_p)
{
  if (real_p && phi_p && param_p)
    return get_all_sets (safe_dyn_cast<phi_info *> (set),
			 REAL_SET | PHI_SET | BB_HEAD_SET | BB_END_SET);

  else if (real_p && param_p)
    return get_all_sets (safe_dyn_cast<phi_info *> (set),
			 REAL_SET | BB_HEAD_SET | BB_END_SET);

  else if (real_p)
    return get_all_sets (safe_dyn_cast<phi_info *> (set), REAL_SET);
  return hash_set<set_info *> ();
}

static bool
source_equal_p (insn_info *insn1, insn_info *insn2)
{
  if (!insn1 || !insn2)
    return false;
  rtx_insn *rinsn1 = insn1->rtl ();
  rtx_insn *rinsn2 = insn2->rtl ();
  if (!rinsn1 || !rinsn2)
    return false;

  rtx note1 = find_reg_equal_equiv_note (rinsn1);
  rtx note2 = find_reg_equal_equiv_note (rinsn2);
  /* We could handle the case of similar-looking REG_EQUALs as well but
     would need to verify that no insn in between modifies any of the source
     operands.  */
  if (note1 && note2 && rtx_equal_p (note1, note2)
      && REG_NOTE_KIND (note1) == REG_EQUIV)
    return true;
  return false;
}

static insn_info *
extract_single_source (set_info *set)
{
  if (!set)
    return nullptr;
  if (set->insn ()->is_real ())
    return set->insn ();
  if (!set->insn ()->is_phi ())
    return nullptr;
  hash_set<set_info *> sets = get_all_sets (set, true, false, true);
  if (sets.is_empty ())
    return nullptr;

  insn_info *first_insn = (*sets.begin ())->insn ();
  if (first_insn->is_artificial ())
    return nullptr;
  for (const set_info *set : sets)
    {
      /* If there is a head or end insn, we conservative return
	 NULL so that VSETVL PASS will insert vsetvl directly.  */
      if (set->insn ()->is_artificial ())
	return nullptr;
      if (set != *sets.begin () && !source_equal_p (set->insn (), first_insn))
	return nullptr;
    }

  return first_insn;
}

static bool
same_equiv_note_p (set_info *set1, set_info *set2)
{
  insn_info *insn1 = extract_single_source (set1);
  insn_info *insn2 = extract_single_source (set2);
  if (!insn1 || !insn2)
    return false;
  return source_equal_p (insn1, insn2);
}

/* Return true if the SET result is not used by any instructions.  */
static bool
has_no_uses (basic_block cfg_bb, rtx_insn *rinsn, int regno)
{
  if (bitmap_bit_p (df_get_live_out (cfg_bb), regno))
    return false;

  rtx_insn *iter;
  for (iter = NEXT_INSN (rinsn); iter && iter != NEXT_INSN (BB_END (cfg_bb));
       iter = NEXT_INSN (iter))
    if (df_find_use (iter, regno_reg_rtx[regno]))
      return false;

  return true;
}

/* Return true for the special block that we can't apply LCM optimization.  */
static bool
invalid_opt_bb_p (basic_block cfg_bb)
{
  edge e;
  edge_iterator ei;

  /* We don't do LCM optimizations on complex edges.  */
  FOR_EACH_EDGE (e, ei, cfg_bb->preds)
    if (e->flags & EDGE_COMPLEX)
      return true;

  /* We only do LCM optimizations on blocks that are post dominated by
     EXIT block, that is, we don't do LCM optimizations on infinite loop.  */
  FOR_EACH_EDGE (e, ei, cfg_bb->succs)
    if ((e->flags & EDGE_FAKE) || (e->flags & EDGE_ABNORMAL))
      return true;

  return false;
}

/* Get all predecessors of BB.  */
static hash_set<basic_block>
get_all_predecessors (basic_block bb)
{
  hash_set<basic_block> blocks;
  auto_vec<basic_block> work_list;
  hash_set<basic_block> visited_list;
  work_list.safe_push (bb);

  while (!work_list.is_empty ())
    {
      basic_block new_bb = work_list.pop ();
      visited_list.add (new_bb);
      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, new_bb->preds)
	{
	  if (!visited_list.contains (e->src))
	    work_list.safe_push (e->src);
	  blocks.add (e->src);
	}
    }
  return blocks;
}

/* This flags indicates the minimum demand of the vl and vtype values by the
   RVV instruction. For example, DEMAND_RATIO_P indicates that this RVV
   instruction only needs the SEW/LMUL ratio to remain the same, and does not
   require SEW and LMUL to be fixed.
   Therefore, if the former RVV instruction needs DEMAND_RATIO_P and the latter
   instruction needs DEMAND_SEW_LMUL_P and its SEW/LMUL is the same as that of
   the former instruction, then we can make the minimum demand of the former
   instruction strict to DEMAND_SEW_LMUL_P, and its required SEW and LMUL are
   the SEW and LMUL of the latter instruction, and the vsetvl instruction
   generated according to the new demand can also be used for the latter
   instruction, so there is no need to insert a separate vsetvl instruction for
   the latter instruction.  */
enum demand_flags : unsigned
{
  DEMAND_EMPTY_P = 0,
  DEMAND_SEW_P = 1 << 0,
  DEMAND_LMUL_P = 1 << 1,
  DEMAND_RATIO_P = 1 << 2,
  DEMAND_GE_SEW_P = 1 << 3,
  DEMAND_TAIL_POLICY_P = 1 << 4,
  DEMAND_MASK_POLICY_P = 1 << 5,
  DEMAND_AVL_P = 1 << 6,
  DEMAND_NON_ZERO_AVL_P = 1 << 7,
  DEMAND_IME_P = 1 << 8,
};

/* We split the demand information into three parts. They are sew and lmul
   related (sew_lmul_demand_type), tail and mask policy related
   (policy_demand_type) and avl related (avl_demand_type). Then we define three
   interfaces available_p, compatible_p and merge. available_p is
   used to determine whether the two vsetvl infos prev_info and next_info are
   available or not. If prev_info is available for next_info, it means that the
   RVV insn corresponding to next_info on the path from prev_info to next_info
   can be used without inserting a separate vsetvl instruction. compatible_p
   is used to determine whether prev_info is compatible with next_info, and if
   so, merge can be used to merge the stricter demand information from
   next_info into prev_info so that prev_info becomes available to next_info.
 */

enum class sew_lmul_demand_type : unsigned
{
  sew_lmul = demand_flags::DEMAND_SEW_P | demand_flags::DEMAND_LMUL_P,
  ratio_only = demand_flags::DEMAND_RATIO_P,
  sew_only = demand_flags::DEMAND_SEW_P,
  ge_sew = demand_flags::DEMAND_GE_SEW_P,
  ratio_and_ge_sew
  = demand_flags::DEMAND_RATIO_P | demand_flags::DEMAND_GE_SEW_P,
};

enum class policy_demand_type : unsigned
{
  tail_mask_policy
  = demand_flags::DEMAND_TAIL_POLICY_P | demand_flags::DEMAND_MASK_POLICY_P,
  tail_policy_only = demand_flags::DEMAND_TAIL_POLICY_P,
  mask_policy_only = demand_flags::DEMAND_MASK_POLICY_P,
  ignore_policy = demand_flags::DEMAND_EMPTY_P,
};

enum class avl_demand_type : unsigned
{
  avl = demand_flags::DEMAND_AVL_P,
  non_zero_avl = demand_flags::DEMAND_NON_ZERO_AVL_P,
  ignore_avl = demand_flags::DEMAND_EMPTY_P,
};

/* Dimensionally separate demand track for the IME vtype bits (lambda,
   altfmt, altfmt_A, altfmt_B, bs).  Non-IME instructions do not care and
   use ignore_ime; IME instructions use ime_vtype and carry the concrete
   tuple in vsetvl_info::m_ime_req.  */
enum class ime_demand_type : unsigned
{
  ime_vtype = demand_flags::DEMAND_IME_P,
  ignore_ime = demand_flags::DEMAND_EMPTY_P,
};

/* IME-specific vtype requirements for an instruction.  Populated only
   when the instruction pattern has its `requires_ime_vtype' attribute
   set to "yes".  The fields reflect the vtype CSR state the IME spec
   demands: the accumulator width (sew / mul_c), the input register-group
   multiplier (lmul), the per-instruction widening factor W, the per-side
   alternate-format selectors, the output format selector, and the
   microscaling block-size selector.

   LAMBDA_LOG2 holds the log2 of the required lambda when VLEN is known
   at compile time, and -1 otherwise (in which case the vsetvl pass must
   emit runtime computation).  */
struct ime_vtype_req
{
  uint8_t sew;
  vlmul_type mul_c;
  uint8_t lmul;
  uint8_t widen_w;
  bool altfmt_a;
  bool altfmt_b;
  bool altfmt;
  bool bs;
  int8_t lambda_log2;

  ime_vtype_req ()
    : sew (0), mul_c (LMUL_RESERVED), lmul (0), widen_w (0),
      altfmt_a (false), altfmt_b (false), altfmt (false), bs (false),
      lambda_log2 (-1)
  {}

  bool operator== (const ime_vtype_req &o) const
  {
    return sew == o.sew && mul_c == o.mul_c && lmul == o.lmul
	   && widen_w == o.widen_w && altfmt_a == o.altfmt_a
	   && altfmt_b == o.altfmt_b && altfmt == o.altfmt && bs == o.bs
	   && lambda_log2 == o.lambda_log2;
  }
  bool operator!= (const ime_vtype_req &o) const { return !(*this == o); }
};

/* Pack an `ime_vtype_req' tuple to ride along as a CONST_INT operand on
   the `@vsetvl_ime*' patterns.  See `pack_ime_req_fields' for the layout.  */
static HOST_WIDE_INT
pack_ime_req (const ime_vtype_req &r)
{
  return pack_ime_req_fields (r.sew, (uint8_t) r.mul_c, r.lmul, r.widen_w,
			      r.altfmt_a, r.altfmt_b, r.altfmt, r.bs,
			      r.lambda_log2);
}

/* Reverse of `pack_ime_req' / `pack_ime_req_fields'.  */
static ime_vtype_req
unpack_ime_req (HOST_WIDE_INT p)
{
  ime_vtype_req r;
  r.sew = (uint8_t) (p & 0xff);
  r.mul_c = (vlmul_type) ((p >> 8) & 0xf);
  r.lmul = (uint8_t) ((p >> 12) & 0xf);
  r.widen_w = (uint8_t) ((p >> 16) & 0xf);
  r.altfmt_a = (p >> 20) & 1;
  r.altfmt_b = (p >> 21) & 1;
  r.altfmt = (p >> 22) & 1;
  r.bs = (p >> 23) & 1;
  r.lambda_log2 = (int8_t) (((int) ((p >> 24) & 0xff)) - 128);
  return r;
}

/* Compute log2(lambda) for an IME instruction with the given accumulator
   SEW_BITS and register-group multiplier MUL_C, using the compile-time
   knowledge of VLEN (if any).  Returns:

     >= 0 : the log2 of the required lambda (so lambda = 1 << result);
	    lambda is always in {1, 2, 4, 8, 16, 32, 64} so the result is
	    in the range [0, 6].
     -1   : VLEN is not compile-time known; the caller must emit a
	    runtime computation.
     -2   : the (VLEN, SEW, MUL_C) triple cannot yield an integer lambda.
	    The caller must raise a diagnostic.  */

static int
compute_ime_lambda_log2 (int sew_bits, int mul_c)
{
  if (!BYTES_PER_RISCV_VECTOR.is_constant ())
    return -1;

  HOST_WIDE_INT vlenb = BYTES_PER_RISCV_VECTOR.to_constant ();
  HOST_WIDE_INT vlen_bits = vlenb * 8;
  HOST_WIDE_INT denom = (HOST_WIDE_INT) sew_bits * mul_c;
  if (denom <= 0 || vlen_bits < denom || vlen_bits % denom != 0)
    return -2;
  HOST_WIDE_INT lambda_sq = vlen_bits / denom;
  if (lambda_sq <= 0 || (lambda_sq & (lambda_sq - 1)) != 0)
    return -2;
  int log_sq = exact_log2 (lambda_sq);
  if (log_sq < 0 || (log_sq & 1) != 0)
    return -2;
  int lambda_log2 = log_sq >> 1;
  if (lambda_log2 < 0 || lambda_log2 > 6)
    return -2;
  return lambda_log2;
}

/* Tracked lambda-DEF state attached to each `vsetvl_info'.

   Plumbing layer for the lambda-aware optimisations that follow:
   commit 3 of this series uses DEFINED to elide redundant mmacc
   auto-vsetvl_ime emissions; commit 4 uses it for cross-block
   vsetlambda DCE and preservation.  In this commit the lattice is
   populated but no optimisation reads it -- behaviour is unchanged.

   States:

     NOT_TOUCHED  The insn does not read or write vtype.lambda.
		  Identity element under merge.

     USE_ONLY     The insn reads lambda but does not write it (e.g.
		  the read-only `__riscv_vsetlambda(0)' path or an
		  explicit csrr vtype).  Does not change the
		  downstream tracked value.

     DEFINED(N)   The insn writes lambda to a statically known value.
		  KNOWN_LOG2 is in [0, 6] (so lambda = 1 << KNOWN_LOG2,
		  i.e. one of {1, 2, 4, 8, 16, 32, 64}); KNOWN_LAMBDA
		  is the same value pre-decoded for convenience.

     KILLED       The insn touches lambda but the post-state is not
		  statically computable.  Catches:
		    - register-form `vsetvl' with opaque rs2;
		    - vsetvli/vsetivli that change SEW or LMUL (WARL
		      re-clamp), at first cut treated unconditionally
		      as KILLED;
		    - calls and asm with vtype clobber;
		    - runtime-arg `__riscv_vsetlambda';
		    - the auto-emitted `vsetvl_ime' helper (which DOES
		      know its target value, but until commit 5 wires
		      the lattice into the IME pipeline we treat it as
		      KILLED to be safe).

   Merge for CFG joins (commit 4):
     same DEFINED(N)  -> DEFINED(N)
     anything else    -> KILLED
     identity         -> NOT_TOUCHED
   USE_ONLY is preserved iff both inputs are USE_ONLY or NOT_TOUCHED.  */

enum class lambda_state_kind : uint8_t
{
  NOT_TOUCHED,
  USE_ONLY,
  DEFINED,          /* Static value known: lambda = 1 << known_log2.  */
  DEFINED_RUNTIME,  /* User established a runtime lambda (via a
		       __riscv_vsetlambda call whose argument is not
		       a compile-time constant): vtype.lambda
		       currently holds the user's chosen value, but
		       that value is unknown to the compiler.  The
		       pass-side emitter for lambda-using consumers
		       treats this distinctly from KILLED: rather
		       than emitting a plain vsetvli (which would
		       WARL-clobber lambda), it emits a sequence that
		       reads vtype.lambda dynamically and merges it
		       with the consumer's static (SEW, LMUL, ta, ma).
		       See `@riscv_vsetvl_preserve_lambda<mode>'.  */
  KILLED
};

struct lambda_state
{
  lambda_state_kind kind;
  int8_t known_log2;  /* Valid iff kind == DEFINED; range [0, 6].  */

  lambda_state () : kind (lambda_state_kind::NOT_TOUCHED), known_log2 (-1) {}

  static lambda_state make_use_only ()
  {
    lambda_state s;
    s.kind = lambda_state_kind::USE_ONLY;
    return s;
  }

  static lambda_state make_killed ()
  {
    lambda_state s;
    s.kind = lambda_state_kind::KILLED;
    return s;
  }

  static lambda_state make_defined (int log2)
  {
    gcc_assert (log2 >= 0 && log2 <= 6);
    lambda_state s;
    s.kind = lambda_state_kind::DEFINED;
    s.known_log2 = (int8_t) log2;
    return s;
  }

  static lambda_state make_defined_runtime ()
  {
    lambda_state s;
    s.kind = lambda_state_kind::DEFINED_RUNTIME;
    return s;
  }

  bool operator== (const lambda_state &o) const
  {
    if (kind != o.kind)
      return false;
    if (kind == lambda_state_kind::DEFINED)
      return known_log2 == o.known_log2;
    return true;
  }
  bool operator!= (const lambda_state &o) const { return !(*this == o); }

  /* CFG-join merge of two reach-states.  Identity is NOT_TOUCHED;
     absorbing element is KILLED; two DEFINED with the same log2
     survive; two DEFINED_RUNTIME survive; mixing static and runtime
     DEFs degrades to KILLED (we cannot prove they refer to the same
     runtime value); everything else degrades to KILLED.  */
  static lambda_state merge (const lambda_state &a, const lambda_state &b)
  {
    if (a.kind == lambda_state_kind::NOT_TOUCHED)
      return b;
    if (b.kind == lambda_state_kind::NOT_TOUCHED)
      return a;
    if (a.kind == lambda_state_kind::KILLED
	|| b.kind == lambda_state_kind::KILLED)
      return make_killed ();
    if (a.kind == lambda_state_kind::USE_ONLY
	&& b.kind == lambda_state_kind::USE_ONLY)
      return make_use_only ();
    if (a.kind == lambda_state_kind::DEFINED
	&& b.kind == lambda_state_kind::DEFINED
	&& a.known_log2 == b.known_log2)
      return a;
    if (a.kind == lambda_state_kind::DEFINED_RUNTIME
	&& b.kind == lambda_state_kind::DEFINED_RUNTIME)
      return make_defined_runtime ();
    /* DEFINED ⊓ USE_ONLY, DEFINED(N) ⊓ DEFINED(M ≠ N), and any mix
       of DEFINED and DEFINED_RUNTIME all degrade to KILLED.  */
    return make_killed ();
  }
};

/* Classify RINSN's effect on vtype.lambda.  This is per-insn local
   classification only -- it does not look at predecessor reach-state.
   The merge logic in `lambda_state::merge' handles join points.

   Returns NOT_TOUCHED for the vast majority of insns; the interesting
   cases are vsetlambda installs (DEFINED) and any other vtype-writing
   insn (KILLED).  */

static lambda_state
classify_lambda_effect (rtx_insn *rinsn)
{
  if (!NONDEBUG_INSN_P (rinsn))
    return lambda_state ();

  /* The vsetlambda install pattern carries its 3-bit encoding as the
     second operand of the UNSPECV_VSETLAMBDA unspec_volatile.  Three
     cases:
       - enc in 1..7: compile-time-constant arg, lambda statically
		      known to be 1 << (enc - 1) -> DEFINED.
       - enc == 0:    runtime arg.  The user has established *some*
		      lambda (the value of their runtime expression),
		      but the compiler does not know what.  This
		      differs from KILLED: a subsequent lambda-using
		      consumer can still observe the user's lambda if
		      the pass uses a vtype-preserving sequence to set
		      up SEW/LMUL -> DEFINED_RUNTIME.
       - anything else: shouldn't happen given the IN_RANGE
		      condition on the MD pattern; fall back to
		      KILLED defensively.  */
  int code = INSN_CODE (rinsn);
  if (code == CODE_FOR_riscv_vsetlambda_installdi
      || code == CODE_FOR_riscv_vsetlambda_installsi)
    {
      extract_insn_cached (rinsn);
      rtx hint = recog_data.operand[1];
      if (CONST_INT_P (hint))
	{
	  HOST_WIDE_INT enc = INTVAL (hint);
	  if (enc >= 1 && enc <= 7)
	    return lambda_state::make_defined ((int) enc - 1);
	  if (enc == 0)
	    return lambda_state::make_defined_runtime ();
	}
      return lambda_state::make_killed ();
    }

  /* The static-VLEN IME auto-emission `@vsetvl_ime<mode>' encodes the
     full vtype (including the 3-bit lambda field) as a const_int in
     operand 2.  Decode it so that back-to-back mmaccs sharing a
     geometry can elide each other's auto-vsetvls: when the walk in
     `lambda_already_established_p' visits a previously-emitted
     `vsetvl_ime', it now recovers DEFINED(N) instead of treating
     the insn as opaque KILLED.

     The runtime-VLEN `@vsetvl_ime_reg<mode>' takes vtype in a register
     instead of a const_int, so we cannot statically extract lambda;
     it stays KILLED.  */
  if (code == CODE_FOR_vsetvl_imedi || code == CODE_FOR_vsetvl_imesi)
    {
      extract_insn_cached (rinsn);
      rtx vtype = recog_data.operand[2];
      if (CONST_INT_P (vtype))
	{
	  int xlen = GET_MODE_BITSIZE (Pmode);
	  HOST_WIDE_INT v = INTVAL (vtype);
	  int enc = (int) ((unsigned HOST_WIDE_INT) v >> (xlen - 4)) & 7;
	  if (enc >= 1 && enc <= 7)
	    return lambda_state::make_defined (enc - 1);
	}
      return lambda_state::make_killed ();
    }

  /* `@riscv_vsetvl_preserve_lambda<mode>' reads vtype.lambda at run
     time and writes the new vtype with the live lambda field merged
     in.  This is a vtype DEF in dataflow terms, but a USE_ONLY of
     lambda (the user's lambda value flows through unchanged): classify
     as DEFINED_RUNTIME so subsequent lambda-USE consumers can elide
     redundant lambda-preserving vsetvls.  */
  if (code == CODE_FOR_riscv_vsetvl_preserve_lambdadi
      || code == CODE_FOR_riscv_vsetvl_preserve_lambdasi)
    return lambda_state::make_defined_runtime ();

  /* Anything else that writes VTYPE_REGNUM is a may-DEF of lambda.
     This includes plain vsetvli/vsetivli (WARL re-clamp on SEW/LMUL
     change), register-form vsetvl, the runtime-VLEN IME helper
     `@vsetvl_ime_reg<mode>', and asm clobbers.
     A more precise classification (e.g. recognise that a vsetvli with
     unchanged SEW/LMUL is not a kill) requires predecessor knowledge
     and is left for commit 4.  */
  df_ref def;
  for (def = DF_INSN_DEFS (rinsn); def; def = DF_REF_NEXT_LOC (def))
    if (DF_REF_REGNO (def) == VTYPE_REGNUM)
      return lambda_state::make_killed ();

  /* Calls and asm conservatively kill vtype (the existing pass already
     treats them as full vtype clobbers; mirror that here).  */
  if (CALL_P (rinsn) || (GET_CODE (PATTERN (rinsn)) == ASM_OPERANDS))
    return lambda_state::make_killed ();

  return lambda_state ();
}

/* Walk backwards from CONSUMER inside its basic block looking for a
   preceding lambda DEF that statically matches DEMAND_LAMBDA_LOG2.
   Returns true if such a DEF is found with no intervening kill of
   the tracked lambda value; the caller can then skip emitting a
   fresh `vsetvl_ime' for the consumer.

   This is intentionally an intra-EBB scan: cross-block reach analysis
   is the job of the LCM data flow already running, and is wired into
   the lambda lattice in a later commit of this series.  Here we want
   the simplest correct optimisation that covers the common
   "user vsetlambda then mmacc" idiom: the install pattern is in the
   same block as the consumer, and the walk-back finds it before
   anything kills lambda.

   The walk is bounded by a small step limit to avoid pathological
   long-block compile-time costs.  */

static bool lambda_reach_at_block_entry_matches_p (basic_block,
						   int demand_lambda_log2);

/* Return the lambda_dataflow's entry state for block BB, or a
   default-constructed NOT_TOUCHED if no dataflow has been computed.
   Defined AFTER the lambda_dataflow class so the body can reach into
   it.  */
static lambda_state lambda_reach_at_block_entry (basic_block);

/* Return the lambda_state that reaches CONSUMER -- the result of the
   intra-block walk back from CONSUMER, falling through to the
   cross-block dataflow's entry state on block-boundary crossing.
   Used by the pass's lambda-USE-aware vsetvl emitter to decide
   between the const-arg vtype-build path (DEFINED), the runtime
   vtype-preserve path (DEFINED_RUNTIME), and a plain vsetvli
   (NOT_TOUCHED / KILLED).  */
static lambda_state lambda_reach_before (rtx_insn *consumer);

/* Return true if RINSN is a USE of vtype.lambda -- i.e. an IME tile
   load/store or multiply-accumulate.  Such an insn must not have its
   lambda silently clobbered by a vtype-config insn inserted between
   the most recent DEF and itself.  */
static bool insn_uses_lambda_p (rtx_insn *rinsn);

static bool
lambda_already_established_p (rtx_insn *consumer, int demand_lambda_log2)
{
  if (!riscv_ime_elide_redundant_vsetvl_lambda)
    return false;
  if (demand_lambda_log2 < 0)
    return false;  /* Runtime-derived demand: cannot elide statically.  */

  const int max_walk = 64;
  basic_block bb = BLOCK_FOR_INSN (consumer);
  if (!bb)
    return false;

  int steps = 0;
  for (rtx_insn *p = PREV_INSN (consumer);
       p && steps < max_walk;
       p = PREV_INSN (p), ++steps)
    {
      if (BLOCK_FOR_INSN (p) != bb)
	{
	  /* Crossed a block boundary.  Fall through to cross-block
	     dataflow if available (commit 4).  */
	  break;
	}
      if (!NONDEBUG_INSN_P (p))
	continue;
      lambda_state s = classify_lambda_effect (p);
      switch (s.kind)
	{
	case lambda_state_kind::DEFINED:
	  /* Found a static DEF.  Elide iff it matches the demand.  */
	  return s.known_log2 == demand_lambda_log2;
	case lambda_state_kind::DEFINED_RUNTIME:
	  /* A runtime DEF establishes some unknown lambda; we cannot
	     prove it matches a statically demanded value, so we cannot
	     elide.  */
	  return false;
	case lambda_state_kind::KILLED:
	  return false;
	case lambda_state_kind::USE_ONLY:
	case lambda_state_kind::NOT_TOUCHED:
	  continue;
	}
    }
  /* Intra-block walk exhausted without resolution.  Consult the
     cross-block dataflow's entry state for this block: if it is a
     matching DEFINED(N) on every predecessor path, the lambda is
     already established at block entry.  */
  return lambda_reach_at_block_entry_matches_p (bb, demand_lambda_log2);
}

/* Return the lambda_state reaching CONSUMER.  Intra-block walk first
   (stopping at the first DEFINED, DEFINED_RUNTIME or KILLED), then
   the cross-block dataflow's entry state if the walk crosses a
   block boundary without resolution.  */

static lambda_state
lambda_reach_before (rtx_insn *consumer)
{
  const int max_walk = 64;
  basic_block bb = BLOCK_FOR_INSN (consumer);
  if (!bb)
    return lambda_state ();

  int steps = 0;
  for (rtx_insn *p = PREV_INSN (consumer);
       p && steps < max_walk;
       p = PREV_INSN (p), ++steps)
    {
      if (BLOCK_FOR_INSN (p) != bb)
	break;
      if (!NONDEBUG_INSN_P (p))
	continue;
      lambda_state s = classify_lambda_effect (p);
      switch (s.kind)
	{
	case lambda_state_kind::DEFINED:
	case lambda_state_kind::DEFINED_RUNTIME:
	case lambda_state_kind::KILLED:
	  return s;
	case lambda_state_kind::USE_ONLY:
	case lambda_state_kind::NOT_TOUCHED:
	  continue;
	}
    }
  /* Cross-block fall-through.  */
  return lambda_reach_at_block_entry (bb);
}

/* True for IME tile loads/stores and multiply-accumulates -- the
   insns that consume vtype.lambda implicitly.  The conservative test
   is `requires_ime_vtype == REQUIRES_IME_VTYPE_YES' (mmacc family);
   tile ops have type `vlds'/'vsts' and read vtype.lambda dynamically.

   `requires_ime_vtype' is the cleanest test where the attribute is
   set; for tile ops that no longer carry it (per the architectural
   change in commit e69d583b9aa0), fall back on the type attribute.  */

static bool
insn_uses_lambda_p (rtx_insn *rinsn)
{
  if (!NONDEBUG_INSN_P (rinsn))
    return false;
  if (recog_memoized (rinsn) < 0)
    return false;
  if (get_attr_requires_ime_vtype (rinsn) == REQUIRES_IME_VTYPE_YES)
    return true;
  enum attr_type t = get_attr_type (rinsn);
  /* vlds/vsts are strided load/store -- the IME tile load/store ops
     are encoded as these types.  Strict gating: only treat them as
     lambda consumers when emitted by the IME tile builtins.  */
  return t == TYPE_VLDS || t == TYPE_VSTS;
}

/* Forward CFG dataflow for the lambda lattice.

   Computes a per-block entry `lambda_state' via a worklist iteration
   to fixpoint over the four-state lattice
   (NOT_TOUCHED / USE_ONLY / DEFINED(N) / KILLED) merged at CFG joins
   by `lambda_state::merge'.  The transfer function within a block is
   `classify_lambda_effect' applied to each insn in order.

   Two consumers:

     (a) Cross-block elision in `lambda_already_established_p': when
	 the intra-block walk reaches block start without resolving the
	 query, the analysis's entry state for that block decides.

     (b) Vsetlambda DCE: while computing each block's exit state, an
	 install whose target encoding matches the running state is
	 redundant; collected into M_DEAD_INSTALLS for removal by the
	 pass.

   The lattice height is finite (4 kinds * 8 possible log2 values for
   DEFINED ≈ 11 distinct points) so the worklist iteration is bounded.
   In practice fixpoint is reached in 1-2 sweeps for typical control
   flow.

   Gated by `-mtrack-user-lambda' (default on).  */

class lambda_dataflow
{
public:
  void run ();
  lambda_state entry_state_of (basic_block bb);
  const auto_vec<rtx_insn *> &dead_installs () const { return m_dead_installs; }

private:
  /* Apply the block's insn-by-insn transfer function starting from
     IN; return the resulting exit state and (side effect) populate
     M_DEAD_INSTALLS with installs that found their target value
     already established at the install point.  */
  lambda_state transfer_block (basic_block bb, lambda_state in,
			       bool collect_dead);

  hash_map<basic_block, lambda_state> m_entry;
  hash_map<basic_block, lambda_state> m_exit;
  auto_vec<rtx_insn *> m_dead_installs;
};

lambda_state
lambda_dataflow::entry_state_of (basic_block bb)
{
  lambda_state *p = m_entry.get (bb);
  return p ? *p : lambda_state ();  /* NOT_TOUCHED if not yet computed.  */
}

lambda_state
lambda_dataflow::transfer_block (basic_block bb, lambda_state in,
				 bool collect_dead)
{
  lambda_state cur = in;
  rtx_insn *insn;
  FOR_BB_INSNS (bb, insn)
    {
      if (!NONDEBUG_INSN_P (insn))
	continue;
      lambda_state s = classify_lambda_effect (insn);
      switch (s.kind)
	{
	case lambda_state_kind::NOT_TOUCHED:
	  break;
	case lambda_state_kind::USE_ONLY:
	  /* Pure read: leaves the tracked DEF alone.  Drops a stale
	     KILLED into nothing -- a `csrr vtype' following an
	     unknown vtype write doesn't suddenly know what the
	     value is.  */
	  break;
	case lambda_state_kind::DEFINED:
	  /* If the install writes the same lambda that is already
	     guaranteed at this point, it is dead.  */
	  if (collect_dead
	      && cur.kind == lambda_state_kind::DEFINED
	      && cur.known_log2 == s.known_log2)
	    m_dead_installs.safe_push (insn);
	  cur = s;
	  break;
	case lambda_state_kind::DEFINED_RUNTIME:
	  /* A runtime install establishes some lambda; we can never
	     prove two runtime installs share a value, so no DCE.  */
	  cur = s;
	  break;
	case lambda_state_kind::KILLED:
	  cur = s;
	  break;
	}
    }
  return cur;
}

void
lambda_dataflow::run ()
{
  /* Single sweep first: seed entry states from prev exit (or
     NOT_TOUCHED for blocks not yet seen) in reverse postorder.
     Then iterate to fixpoint.  */
  basic_block bb;

  auto compute_entry = [&] (basic_block b) -> lambda_state {
    if (b == ENTRY_BLOCK_PTR_FOR_FN (cfun))
      return lambda_state ();
    lambda_state acc;
    bool any = false;
    edge e;
    edge_iterator ei;
    FOR_EACH_EDGE (e, ei, b->preds)
      {
	const lambda_state *p = m_exit.get (e->src);
	if (!p)
	  continue;
	acc = any ? lambda_state::merge (acc, *p) : *p;
	any = true;
      }
    return any ? acc : lambda_state ();
  };

  /* Worklist: blocks whose successors must be re-examined.  Use a
     small visited-set to avoid runaway re-adds.  Capped iteration
     to defend against pathological CFGs.  */
  auto_bitmap worklist;
  FOR_EACH_BB_FN (bb, cfun)
    bitmap_set_bit (worklist, bb->index);

  int iter_cap = 100 * n_basic_blocks_for_fn (cfun) + 32;
  while (!bitmap_empty_p (worklist) && iter_cap-- > 0)
    {
      int idx = bitmap_first_set_bit (worklist);
      bitmap_clear_bit (worklist, idx);
      bb = BASIC_BLOCK_FOR_FN (cfun, idx);
      if (!bb)
	continue;

      lambda_state new_entry = compute_entry (bb);
      lambda_state new_exit = transfer_block (bb, new_entry,
					      /*collect_dead=*/false);

      const lambda_state *old_entry = m_entry.get (bb);
      const lambda_state *old_exit = m_exit.get (bb);
      bool changed = (!old_entry || *old_entry != new_entry
		      || !old_exit || *old_exit != new_exit);

      m_entry.put (bb, new_entry);
      m_exit.put (bb, new_exit);

      if (changed)
	{
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->dest != EXIT_BLOCK_PTR_FOR_FN (cfun))
	      bitmap_set_bit (worklist, e->dest->index);
	}
    }

  /* Final sweep with collect_dead=true: now that entry states are at
     fixpoint, the in-block transfer can identify redundant installs
     reliably.  */
  m_dead_installs.truncate (0);
  FOR_EACH_BB_FN (bb, cfun)
    {
      lambda_state in = compute_entry (bb);
      transfer_block (bb, in, /*collect_dead=*/true);
    }
}

/* Singleton lambda dataflow used by the elision and DCE paths.  Set
   up by `pass_vsetvl::execute' before any IME emission, cleared at
   pass end.  */
static lambda_dataflow *g_lambda_df;


/* Cross-block extension of `lambda_already_established_p': if the
   intra-block walk reaches block start without resolving, consult the
   dataflow's entry state for the block.  Returns true iff the
   computed entry state is DEFINED(N) with N matching the demand.  */

static bool
lambda_reach_at_block_entry_matches_p (basic_block bb,
				       int demand_lambda_log2)
{
  if (!g_lambda_df)
    return false;
  lambda_state s = g_lambda_df->entry_state_of (bb);
  return s.kind == lambda_state_kind::DEFINED
	 && s.known_log2 == demand_lambda_log2;
}

/* Return the dataflow's entry state for BB, or NOT_TOUCHED if no
   dataflow has been computed.  Used by `lambda_reach_before' when
   the intra-block walk crosses the block boundary.  */
static lambda_state
lambda_reach_at_block_entry (basic_block bb)
{
  if (!g_lambda_df)
    return lambda_state ();
  return g_lambda_df->entry_state_of (bb);
}

/* Extract the IME vtype requirements for RINSN.  RINSN must have its
   `requires_ime_vtype' attribute set to yes.  The returned record is
   fully determined by RINSN's operand modes; see §C-Language Intrinsics
   of the IME specification for the mapping rules.

   The current implementation handles the integer vmmacc.vv family
   (Zvvmm, W=1).  Integer signedness (altfmt_A / altfmt_B) is currently
   hardcoded to 0 (signed) because the RTL pattern does not yet
   distinguish between vint/vuint operands; that split will come with a
   signed/unsigned UNSPEC separation in a follow-up.  Widening variants,
   FP mmacc and microscaled variants each extend this function with
   their own derivation rules.  */
static ime_vtype_req
riscv_extract_ime_requirements (rtx_insn *rinsn)
{
  ime_vtype_req r;

  /* Callers may have invalidated recog_data for other insns between
     the original extract_insn_cached(rinsn) and us.  Re-extract.  */
  extract_insn_cached (rinsn);

  /* The data-vector mode tells us SEW and (for mmacc) MUL_C.  For
     load-shaped patterns (mmacc, vmtl) operand 0 is the vector
     destination.  For store-shaped patterns (vmts) operand 0 is a
     memory_operand -- fall back to the first vector register
     operand instead.  */
  machine_mode acc_mode = VOIDmode;
  for (int i = 0; i < recog_data.n_operands; i++)
    if (REG_P (recog_data.operand[i])
	&& VECTOR_MODE_P (GET_MODE (recog_data.operand[i])))
      {
	acc_mode = GET_MODE (recog_data.operand[i]);
	break;
      }
  gcc_assert (acc_mode != VOIDmode);
  r.sew = GET_MODE_UNIT_BITSIZE (acc_mode);

  /* Tile load/store instructions (vmtl.v, vmts.v, vmttl.v, vmtts.v)
     move data between memory and a plain vector register group; there
     is no accumulator, so the lambda constraint is computed with a
     notional MUL_C=1.  The LMUL of the data vector is carried through
     the vtype LMUL field separately, not folded into lambda.
     The mmacc family uses the data register's LMUL as MUL_C because
     its vd really is the accumulator register group.  */
  enum attr_type t = get_attr_type (rinsn);
  bool is_tile = (t == TYPE_VLDS || t == TYPE_VSTS);
  if (is_tile)
    r.mul_c = riscv_vector::LMUL_1;
  else
    r.mul_c = get_vlmul (acc_mode);

  /* For the mmacc family operand 2 is vs1 and may have a different
     LMUL (input LMUL) and/or a narrower SEW (widening variants).
     For tile load/store the stride operand is a scalar register, so
     fall back to the data-vector mode.  */
  machine_mode in_mode = acc_mode;
  if (recog_data.n_operands > 2
      && REG_P (recog_data.operand[2])
      && VECTOR_MODE_P (GET_MODE (recog_data.operand[2])))
    in_mode = GET_MODE (recog_data.operand[2]);
  uint8_t in_sew = GET_MODE_UNIT_BITSIZE (in_mode);
  r.lmul = get_vlmul (in_mode);
  gcc_assert (r.sew > 0 && in_sew > 0 && r.sew % in_sew == 0);
  r.widen_w = r.sew / in_sew;

  /* Integer: altfmt (output) always 0.  Signedness comes from the
     per-pattern ime_altfmt attribute set by the RTL pattern.  For
     now we only support same-sign (ss, uu); mixed-sign (su, us)
     will be added with the widening mmacc variants.  */
  r.altfmt = false;

  /* Read the block-scale size from the ime_bs attribute.  */
  switch (get_attr_ime_bs (rinsn))
    {
    case IME_BS_BS32:
      r.bs = true;
      break;
    case IME_BS_BS16:
    case IME_BS_NONE:
      r.bs = false;
      break;
    default:
      gcc_unreachable ();
    }

  switch (get_attr_ime_altfmt (rinsn))
    {
    case IME_ALTFMT_SS:
      r.altfmt_a = false;
      r.altfmt_b = false;
      break;
    case IME_ALTFMT_UU:
      r.altfmt_a = true;
      r.altfmt_b = true;
      break;
    case IME_ALTFMT_SU:
      r.altfmt_a = false;
      r.altfmt_b = true;
      break;
    case IME_ALTFMT_US:
      r.altfmt_a = true;
      r.altfmt_b = false;
      break;
    default:
      gcc_unreachable ();
    }

  /* For FP mmacc, the altfmt bits select the alternative IEEE format at
     the relevant SEW: BFmode at SEW=16 selects bf16, HFmode selects fp16.
     Per the IME spec / QEMU dispatch fns[altfmt_a][altfmt_b][altfmt][sew]:
       altfmt_a -- vs1 input format (per-operand)
       altfmt_b -- vs2 input format (per-operand)
       altfmt   -- accumulator/output format
     W=1 same-format bf16 requires all three set; widening bf16->fp32
     sets altfmt_{a,b} only.  Mixed-format intrinsics (e.g. fp16 x bf16)
     would set altfmt_a and altfmt_b differently -- inspect each input
     operand independently so the same code handles them when added.
     OFP8 (E4M3 vs E5M2) at SEW=8 cannot be told apart from the mode
     alone -- those go through dedicated UNSPECs and will need
     attribute-driven dispatch in a follow-up.  */
  if (GET_MODE_INNER (acc_mode) == E_BFmode)
    r.altfmt = true;
  if (recog_data.n_operands > 2
      && REG_P (recog_data.operand[2])
      && VECTOR_MODE_P (GET_MODE (recog_data.operand[2]))
      && GET_MODE_INNER (GET_MODE (recog_data.operand[2])) == E_BFmode)
    r.altfmt_a = true;
  if (recog_data.n_operands > 3
      && REG_P (recog_data.operand[3])
      && VECTOR_MODE_P (GET_MODE (recog_data.operand[3]))
      && GET_MODE_INNER (GET_MODE (recog_data.operand[3])) == E_BFmode)
    r.altfmt_b = true;

  /* OFP8 / explicit FP altfmt override.  When the pattern carries a
     non-"none" ime_fp_altfmt attribute, overwrite the three altfmt
     bits we just inferred -- needed for OFP8 (E4M3 vs E5M2) at SEW=8
     where both formats share RVVM*QI mode and the BFmode inference
     above cannot tell them apart.  The encoding is "<A>_<B>_<F>",
     each token e4m3 (=0) or e5m2 (=1).  */
  switch (get_attr_ime_fp_altfmt (rinsn))
    {
    case IME_FP_ALTFMT_NONE:
      break;  /* keep BFmode/integer inference */
    case IME_FP_ALTFMT_E4M3_E4M3_E4M3:
      r.altfmt_a = false; r.altfmt_b = false; r.altfmt = false; break;
    case IME_FP_ALTFMT_E4M3_E4M3_E5M2:
      r.altfmt_a = false; r.altfmt_b = false; r.altfmt = true;  break;
    case IME_FP_ALTFMT_E4M3_E5M2_E4M3:
      r.altfmt_a = false; r.altfmt_b = true;  r.altfmt = false; break;
    case IME_FP_ALTFMT_E4M3_E5M2_E5M2:
      r.altfmt_a = false; r.altfmt_b = true;  r.altfmt = true;  break;
    case IME_FP_ALTFMT_E5M2_E4M3_E4M3:
      r.altfmt_a = true;  r.altfmt_b = false; r.altfmt = false; break;
    case IME_FP_ALTFMT_E5M2_E4M3_E5M2:
      r.altfmt_a = true;  r.altfmt_b = false; r.altfmt = true;  break;
    case IME_FP_ALTFMT_E5M2_E5M2_E4M3:
      r.altfmt_a = true;  r.altfmt_b = true;  r.altfmt = false; break;
    case IME_FP_ALTFMT_E5M2_E5M2_E5M2:
      r.altfmt_a = true;  r.altfmt_b = true;  r.altfmt = true;  break;
    default:
      gcc_unreachable ();
    }

  /* Static lambda if VLEN is known; runtime otherwise.  */
  int mul_c_val;
  switch (r.mul_c)
    {
    case LMUL_1:  mul_c_val = 1; break;
    case LMUL_2:  mul_c_val = 2; break;
    case LMUL_4:  mul_c_val = 4; break;
    case LMUL_8:  mul_c_val = 8; break;
    case LMUL_16: mul_c_val = 16; break;
    default:      mul_c_val = 0; break;  /* Fractional or reserved: invalid.  */
    }
  if (mul_c_val == 0)
    r.lambda_log2 = -2;
  else
    r.lambda_log2 = compute_ime_lambda_log2 (r.sew, mul_c_val);

  return r;
}

/* Forward declarations for helpers that need vsetvl_info; the
   definitions come right after the class, below.  */
class vsetvl_info;
static rtx emit_ime_vtype_build (const vsetvl_info &info);

/* Go through all uses of INSN looking for a single use of register REG.
   Return true if we find
    - Uses in a non-RVV insn
    - More than one use in an RVV insn
    - A single use in the VL operand of an RVV insn
   and false otherwise.
   A single use in the AVL operand does not count as use as we take care of
   those separately in the pass.  */

static bool
reg_used (insn_info *insn, rtx reg)
{
  unsigned int regno = REGNO (reg);
  const hash_set<use_info *> vl_uses = get_all_real_uses (insn, regno);
  for (use_info *use : vl_uses)
    {
      gcc_assert (use->insn ()->is_real ());
      rtx_insn *rinsn = use->insn ()->rtl ();
      if (!has_vl_op (rinsn)
	  || count_regno_occurrences (rinsn, regno) != 1)
	return true;

      rtx avl = ::get_avl (rinsn);
      if (!avl || !REG_P (avl) || regno != REGNO (avl))
	return true;
    }
  return false;
}


class vsetvl_info
{
private:
  insn_info *m_insn;
  bb_info *m_bb;
  rtx m_avl;
  rtx m_vl;
  set_info *m_avl_def;
  uint8_t m_sew;
  uint8_t m_max_sew;
  vlmul_type m_vlmul;
  uint8_t m_ratio;
  bool m_ta;
  bool m_ma;

  sew_lmul_demand_type m_sew_lmul_demand;
  policy_demand_type m_policy_demand;
  avl_demand_type m_avl_demand;

  enum class state_type
  {
    UNINITIALIZED,
    VALID,
    UNKNOWN,
    EMPTY,
  };
  state_type m_state;

  bool m_delete;
  bool m_change_vtype_only;
  insn_info *m_read_vl_insn;
  bool m_vl_used_by_non_rvv_insn;

  /* IME state.  M_IME_REQUIRED is true when the instruction pattern has
     `requires_ime_vtype=yes'; M_IME_REQ holds the concrete tuple in that
     case.  M_IME_DEMAND is the demand dimension: ime_vtype when the
     instruction needs the IME vtype bits established, ignore_ime
     otherwise.  */
  bool m_ime_required;
  ime_vtype_req m_ime_req;
  ime_demand_type m_ime_demand;

  /* Per-insn classification of vtype.lambda effect.  See the
     `lambda_state' header comment near `classify_lambda_effect'.
     Populated by `parse_insn'; consumed by lambda-aware optimisations
     introduced in later commits of this series.  */
  lambda_state m_lambda;

public:
  vsetvl_info ()
    : m_insn (nullptr), m_bb (nullptr), m_avl (NULL_RTX), m_vl (NULL_RTX),
      m_avl_def (nullptr), m_sew (0), m_max_sew (0), m_vlmul (LMUL_RESERVED),
      m_ratio (0), m_ta (false), m_ma (false),
      m_sew_lmul_demand (sew_lmul_demand_type::sew_lmul),
      m_policy_demand (policy_demand_type::tail_mask_policy),
      m_avl_demand (avl_demand_type::avl), m_state (state_type::UNINITIALIZED),
      m_delete (false), m_change_vtype_only (false), m_read_vl_insn (nullptr),
      m_vl_used_by_non_rvv_insn (false), m_ime_required (false), m_ime_req (),
      m_ime_demand (ime_demand_type::ignore_ime)
  {}

  vsetvl_info (insn_info *insn) : vsetvl_info () { parse_insn (insn); }

  vsetvl_info (rtx_insn *insn) : vsetvl_info () { parse_insn (insn); }

  void set_avl (rtx avl) { m_avl = avl; }
  void set_vl (rtx vl) { m_vl = vl; }
  void set_avl_def (set_info *avl_def) { m_avl_def = avl_def; }
  void set_sew (uint8_t sew) { m_sew = sew; }
  void set_vlmul (vlmul_type vlmul) { m_vlmul = vlmul; }
  void set_ratio (uint8_t ratio) { m_ratio = ratio; }
  void set_ta (bool ta) { m_ta = ta; }
  void set_ma (bool ma) { m_ma = ma; }
  void set_delete () { m_delete = true; }
  void set_bb (bb_info *bb) { m_bb = bb; }
  void set_max_sew (uint8_t max_sew) { m_max_sew = max_sew; }
  void set_change_vtype_only () { m_change_vtype_only = true; }
  void set_read_vl_insn (insn_info *insn) { m_read_vl_insn = insn; }

  rtx get_avl () const { return m_avl; }
  rtx get_vl () const { return m_vl; }
  set_info *get_avl_def () const { return m_avl_def; }
  uint8_t get_sew () const { return m_sew; }
  vlmul_type get_vlmul () const { return m_vlmul; }
  uint8_t get_ratio () const { return m_ratio; }
  bool get_ta () const { return m_ta; }
  bool get_ma () const { return m_ma; }
  insn_info *get_insn () const { return m_insn; }
  bool delete_p () const { return m_delete; }
  bb_info *get_bb () const { return m_bb; }
  uint8_t get_max_sew () const { return m_max_sew; }
  insn_info *get_read_vl_insn () const { return m_read_vl_insn; }
  bool vl_used_by_non_rvv_insn_p () const { return m_vl_used_by_non_rvv_insn; }

  bool ime_required_p () const { return m_ime_required; }
  const ime_vtype_req &get_ime_req () const { return m_ime_req; }
  void set_ime_required (bool r) { m_ime_required = r; }
  void set_ime_req (const ime_vtype_req &r) { m_ime_req = r; }
  ime_demand_type get_ime_demand () const { return m_ime_demand; }
  void set_ime_demand (ime_demand_type demand) { m_ime_demand = demand; }

  const lambda_state &get_lambda () const { return m_lambda; }
  void set_lambda (const lambda_state &l) { m_lambda = l; }

  bool has_imm_avl () const { return m_avl && CONST_INT_P (m_avl); }
  bool has_vlmax_avl () const { return vlmax_avl_p (m_avl); }
  bool has_nonvlmax_reg_avl () const
  {
    return m_avl && REG_P (m_avl) && !has_vlmax_avl ();
  }
  bool has_non_zero_avl () const
  {
    if (has_imm_avl ())
      return INTVAL (m_avl) > 0;
    return has_vlmax_avl ();
  }
  bool has_vl () const
  {
    /* The VL operand can only be either a NULL_RTX or a register.   */
    gcc_assert (!m_vl || REG_P (m_vl));
    return m_vl != NULL_RTX;
  }
  bool has_same_ratio (const vsetvl_info &other) const
  {
    return get_ratio () == other.get_ratio ();
  }

  /* The block of INSN isn't always same as the block of the VSETVL_INFO,
      meaning we may have 'get_insn ()->bb () != get_bb ()'.

	E.g.  BB 2 (Empty) ---> BB 3 (VALID, has rvv insn 1)

     BB 2 has empty VSETVL_INFO, wheras BB 3 has VSETVL_INFO that satisfies
     get_insn ()->bb () == get_bb (). In earliest fusion, we may fuse bb 3 and
     bb 2 so that the 'get_bb ()' of BB2 VSETVL_INFO will be BB2 wheras the
     'get_insn ()' of BB2 VSETVL INFO will be the rvv insn 1 (which is located
     at BB3).  */
  bool insn_inside_bb_p () const { return get_insn ()->bb () == get_bb (); }
  void update_avl (const vsetvl_info &other)
  {
    m_avl = other.get_avl ();
    m_vl = other.get_vl ();
    m_avl_def = other.get_avl_def ();
  }

  bool uninit_p () const { return m_state == state_type::UNINITIALIZED; }
  bool valid_p () const { return m_state == state_type::VALID; }
  bool unknown_p () const { return m_state == state_type::UNKNOWN; }
  bool empty_p () const { return m_state == state_type::EMPTY; }
  bool change_vtype_only_p () const { return m_change_vtype_only
					     && !TARGET_XTHEADVECTOR; }

  void set_valid () { m_state = state_type::VALID; }
  void set_unknown () { m_state = state_type::UNKNOWN; }
  void set_empty () { m_state = state_type::EMPTY; }

  void set_sew_lmul_demand (sew_lmul_demand_type demand)
  {
    m_sew_lmul_demand = demand;
  }
  void set_policy_demand (policy_demand_type demand)
  {
    m_policy_demand = demand;
  }
  void set_avl_demand (avl_demand_type demand) { m_avl_demand = demand; }

  sew_lmul_demand_type get_sew_lmul_demand () const
  {
    return m_sew_lmul_demand;
  }
  policy_demand_type get_policy_demand () const { return m_policy_demand; }
  avl_demand_type get_avl_demand () const { return m_avl_demand; }

  void normalize_demand (unsigned demand_flags)
  {
    switch (demand_flags
	    & (DEMAND_SEW_P | DEMAND_LMUL_P | DEMAND_RATIO_P | DEMAND_GE_SEW_P))
      {
      case (unsigned) sew_lmul_demand_type::sew_lmul:
	m_sew_lmul_demand = sew_lmul_demand_type::sew_lmul;
	break;
      case (unsigned) sew_lmul_demand_type::ratio_only:
	m_sew_lmul_demand = sew_lmul_demand_type::ratio_only;
	break;
      case (unsigned) sew_lmul_demand_type::sew_only:
	m_sew_lmul_demand = sew_lmul_demand_type::sew_only;
	break;
      case (unsigned) sew_lmul_demand_type::ge_sew:
	m_sew_lmul_demand = sew_lmul_demand_type::ge_sew;
	break;
      case (unsigned) sew_lmul_demand_type::ratio_and_ge_sew:
	m_sew_lmul_demand = sew_lmul_demand_type::ratio_and_ge_sew;
	break;
      default:
	gcc_unreachable ();
      }

    switch (demand_flags & (DEMAND_TAIL_POLICY_P | DEMAND_MASK_POLICY_P))
      {
      case (unsigned) policy_demand_type::tail_mask_policy:
	m_policy_demand = policy_demand_type::tail_mask_policy;
	break;
      case (unsigned) policy_demand_type::tail_policy_only:
	m_policy_demand = policy_demand_type::tail_policy_only;
	break;
      case (unsigned) policy_demand_type::mask_policy_only:
	m_policy_demand = policy_demand_type::mask_policy_only;
	break;
      case (unsigned) policy_demand_type::ignore_policy:
	m_policy_demand = policy_demand_type::ignore_policy;
	break;
      default:
	gcc_unreachable ();
      }

    switch (demand_flags & (DEMAND_AVL_P | DEMAND_NON_ZERO_AVL_P))
      {
      case (unsigned) avl_demand_type::avl:
	m_avl_demand = avl_demand_type::avl;
	break;
      case (unsigned) avl_demand_type::non_zero_avl:
	m_avl_demand = avl_demand_type::non_zero_avl;
	break;
      case (unsigned) avl_demand_type::ignore_avl:
	m_avl_demand = avl_demand_type::ignore_avl;
	break;
      default:
	gcc_unreachable ();
      }

    m_ime_demand = (demand_flags & DEMAND_IME_P)
		     ? ime_demand_type::ime_vtype
		     : ime_demand_type::ignore_ime;
  }

  void parse_insn (rtx_insn *rinsn)
  {
    if (!NONDEBUG_INSN_P (rinsn))
      return;
    if (optimize == 0 && !has_vtype_op (rinsn))
      return;
    gcc_assert (!vsetvl_discard_result_insn_p (rinsn));

    /* `riscv_vsetlambda_install' is transparent for the SEW/LMUL/
       policy/AVL demand analysis: it only modifies vtype.lambda.
       Leave m_state UNINITIALIZED (no demand opinion) and just record
       the lambda effect; see the corresponding branch in
       `parse_insn(insn_info *)' for the full rationale.

       Use `recog_memoized' (not raw INSN_CODE) so freshly emitted
       insns -- e.g. the helper patterns inserted by
       `fixup_lambda_clobbering_placeholders' -- get recognised here
       rather than silently falling through.  */
    int code = recog_memoized (rinsn);
    if (code == CODE_FOR_riscv_vsetlambda_installdi
	|| code == CODE_FOR_riscv_vsetlambda_installsi)
      {
	m_lambda = classify_lambda_effect (rinsn);
	return;
      }

    /* `@riscv_vsetvl_preserve_lambda<mode>' is a transparent SEW/LMUL
       writer: decode its const_int vtype operand (operand 2) and
       populate the demand fields with the same `ratio_only' profile a
       regular `@vsetvl<mode>' source would have.  This lets the LCM
       see the insn as an available vtype source for downstream
       lambda-USE consumers with matching geometry -- the redundant
       second lambda-preserving emission then naturally disappears.

       Note that `get_avl' returns NULL_RTX for preserve_lambda
       (no vl_op_idx attr), so we extract operand 1 directly.  The
       `is_preserve_lambda_p' guards in `emit_vsetvl' /
       `pre_global_vsetvl_info' ensure the pass never tries to
       *modify* or *insert before* a preserve_lambda based on this
       VALID classification.  */
    if (code == CODE_FOR_riscv_vsetvl_preserve_lambdadi
	|| code == CODE_FOR_riscv_vsetvl_preserve_lambdasi)
      {
	extract_insn_cached (rinsn);
	HOST_WIDE_INT vtype_const = INTVAL (recog_data.operand[2]);
	uint8_t sew;
	vlmul_type vlmul;
	bool ta_bit, ma_bit;
	if (decode_static_vtype_const (vtype_const, &sew, &vlmul, &ta_bit, &ma_bit))
	  {
	    set_valid ();
	    m_avl = recog_data.operand[1];
	    m_vl = recog_data.operand[0];
	    m_sew = sew;
	    m_vlmul = vlmul;
	    m_ta = ta_bit;
	    m_ma = ma_bit;
	    m_ratio = calculate_ratio (m_sew, m_vlmul);
	    m_max_sew = get_max_int_sew ();
	    normalize_demand (demand_flags::DEMAND_AVL_P
			      | demand_flags::DEMAND_RATIO_P);
	  }
	else
	  set_unknown ();
	m_lambda = lambda_state::make_defined_runtime ();
	return;
      }

    /* `@read_vtype<mode>': pure vtype READ.  It does NOT write vtype, so
       leave the demand state uninitialised (the BB's reaching IME vsetvl
       remains the effective vtype source).  Only record the lambda
       use-only effect.  Marking this UNKNOWN here used to break the BB
       footer chain right after the runtime probe, hiding the IME vsetvl
       from downstream demand matching.  */
    if (code == CODE_FOR_read_vtypedi || code == CODE_FOR_read_vtypesi)
      {
	m_lambda = lambda_state::make_use_only ();
	return;
      }

    /* IME vsetvl helpers (`@vsetvl_ime<mode>', `@vsetvl_ime_reg<mode>')
       carry a packed IME-demand operand (operand 3).  Decode it and the
       static vtype const (operand 2, for the static-path variant) to
       expose the insn as a VALID, demand-carrying vsetvl source; a
       downstream consumer with matching demand will then see the IME
       vtype as available and the pass will not insert a redundant
       second IME vsetvl right before it.

       A sentinel of operand 3 == 0 (sew == 0 in the unpacked tuple)
       means "no IME demand" -- this is how the lambda-preserving
       emissions for non-IME consumers ride the same pattern; treat
       those as basic SEW/LMUL writers with no IME bookkeeping.  */
    if (code == CODE_FOR_vsetvl_imedi || code == CODE_FOR_vsetvl_imesi
	|| code == CODE_FOR_vsetvl_ime_regdi
	|| code == CODE_FOR_vsetvl_ime_regsi)
      {
	extract_insn_cached (rinsn);
	bool is_static_path = (code == CODE_FOR_vsetvl_imedi
			       || code == CODE_FOR_vsetvl_imesi);
	HOST_WIDE_INT ime_pack = INTVAL (recog_data.operand[3]);
	ime_vtype_req req = unpack_ime_req (ime_pack);
	bool have_ime_demand = (req.sew != 0);

	uint8_t sew = 0;
	vlmul_type vlmul = LMUL_RESERVED;
	bool ta_bit = true, ma_bit = true;

	if (is_static_path)
	  {
	    HOST_WIDE_INT vtype_const = INTVAL (recog_data.operand[2]);
	    if (!decode_static_vtype_const (vtype_const, &sew, &vlmul,
					    &ta_bit, &ma_bit))
	      {
		set_unknown ();
		m_lambda = lambda_state::make_killed ();
		return;
	      }
	  }
	else if (have_ime_demand)
	  {
	    /* Runtime path: the basic vtype fields are determined by the
	       IME demand the caller asked for (sew/lmul/ta/ma).  */
	    sew = req.sew;
	    vlmul = (vlmul_type) req.lmul;
	    /* IME emissions are always tail/mask agnostic by construction
	       (see emit_ime_runtime_vtype_setup: ta = true, ma = true).  */
	    ta_bit = ma_bit = true;
	  }
	else
	  {
	    /* Runtime path with no IME demand: should not occur (the
	       runtime variant is only emitted from IME consumer expand).  */
	    set_unknown ();
	    m_lambda = lambda_state::make_killed ();
	    return;
	  }

	set_valid ();
	m_avl = recog_data.operand[1];
	m_vl = recog_data.operand[0];
	m_sew = sew;
	m_vlmul = vlmul;
	m_ta = ta_bit;
	m_ma = ma_bit;
	m_ratio = calculate_ratio (m_sew, m_vlmul);
	m_max_sew = get_max_int_sew ();
	unsigned dflags = demand_flags::DEMAND_AVL_P;
	if (have_ime_demand)
	  {
	    m_ime_required = true;
	    m_ime_req = req;
	    /* For an IME mmacc, the physically encoded vtype.LMUL is the
	       *input* LMUL (m_ime_req.lmul).  Mirror the logic in the
	       generic-recog path below so downstream sew_lmul checks
	       observe the same effective LMUL.  */
	    m_vlmul = (vlmul_type) m_ime_req.lmul;
	    m_ratio = calculate_ratio (m_sew, m_vlmul);
	    dflags |= demand_flags::DEMAND_SEW_P
		      | demand_flags::DEMAND_LMUL_P
		      | demand_flags::DEMAND_IME_P;
	  }
	else
	  dflags |= demand_flags::DEMAND_RATIO_P;
	normalize_demand (dflags);
	m_lambda = classify_lambda_effect (rinsn);
	return;
      }

    set_valid ();
    extract_insn_cached (rinsn);
    m_avl = ::get_avl (rinsn);
    if (has_vlmax_avl () || vsetvl_insn_p (rinsn))
      m_vl = ::get_vl (rinsn);
    m_sew = ::get_sew (rinsn);
    m_vlmul = ::get_vlmul (rinsn);
    m_ta = tail_agnostic_p (rinsn);
    m_ma = mask_agnostic_p (rinsn);
    if (get_attr_requires_ime_vtype (rinsn) == REQUIRES_IME_VTYPE_YES)
      {
	m_ime_required = true;
	m_ime_req = riscv_extract_ime_requirements (rinsn);
	m_ime_demand = ime_demand_type::ime_vtype;
	/* For an IME mmacc, the physically encoded vtype.LMUL is the
	   *input* LMUL (m_ime_req.lmul), not the data-mode LMUL that
	   ::get_vlmul returned above (which is the accumulator's MUL_C
	   for widening variants).  Sync m_vlmul/m_ratio with what the
	   vsetvl_ime emission actually writes, so sew_lmul compatibility
	   checks against a subsequent non-IME consumer (e.g. vmts.v
	   demanding VLMUL=m4 after a widening vfwmmacc that left
	   vtype.LMUL=m1) correctly observe the mismatch and emit a
	   vtype change.  Non-widening mmacc has input LMUL == acc LMUL
	   so the override is a no-op there.  */
	m_vlmul = (vlmul_type) m_ime_req.lmul;
	m_ratio = calculate_ratio (m_sew, m_vlmul);
      }
    m_lambda = classify_lambda_effect (rinsn);
  }

  void parse_insn (insn_info *insn)
  {
    /* The VL dest of the insn */
    rtx dest_vl = NULL_RTX;

    m_insn = insn;
    m_bb = insn->bb ();
    /* Return if it is debug insn for the consistency with optimize == 0.  */
    if (insn->is_debug_insn ())
      return;

    /* We set it as unknown since we don't what will happen in CALL or ASM.  */
    if (insn->is_call () || insn->is_asm ())
      {
	set_unknown ();
	/* Conservative: calls and asm may clobber vtype.lambda.  */
	m_lambda = lambda_state::make_killed ();
	return;
      }

    /* The IME vsetvl helpers (`@vsetvl_ime<mode>',
       `@vsetvl_ime_reg<mode>') and `@read_vtype<mode>' write the full
       vtype as a single Pmode value rather than the
       sew/lmul/ta/ma operand tuple of `@vsetvl<mode>'.  Treat them as
       opaque vtype writes: do not try to extract sew/vlmul (the
       computation below would call `calculate_ratio (255, 255)' and
       ICE).  Mark the state as unknown so any consumer that follows
       gets a fresh vsetvl insertion as needed.  */
    /* Force recog if the insn hasn't been recognised yet (freshly emitted
       insns -- e.g. by `fixup_lambda_clobbering_placeholders' -- have
       INSN_CODE == -1 until something runs recog on them).  Without this,
       the early-return cases below would silently fall through and the
       sew/vlmul extraction would ICE on the helper patterns.  */
    int code = recog_memoized (insn->rtl ());

    /* `@riscv_vsetvl_preserve_lambda<mode>': decode operand 2 into the
       SEW/LMUL/ta/ma fields the rest of the pass uses, mark VALID
       with a `ratio_only' demand profile, and let the LCM treat this
       insn as a regular vsetvl source for downstream consumers.  The
       `is_preserve_lambda_p' guards elsewhere ensure the pass does
       not try to modify or insert before this insn.  See the matching
       branch in `parse_insn(rtx_insn *)' for full rationale.  */
    if (code == CODE_FOR_riscv_vsetvl_preserve_lambdadi
	|| code == CODE_FOR_riscv_vsetvl_preserve_lambdasi)
      {
	extract_insn_cached (insn->rtl ());
	HOST_WIDE_INT vtype_const = INTVAL (recog_data.operand[2]);
	uint8_t sew;
	vlmul_type vlmul;
	bool ta_bit, ma_bit;
	if (decode_static_vtype_const (vtype_const, &sew, &vlmul, &ta_bit, &ma_bit))
	  {
	    set_valid ();
	    m_avl = recog_data.operand[1];
	    m_vl = recog_data.operand[0];
	    dest_vl = m_vl;
	    m_sew = sew;
	    m_vlmul = vlmul;
	    m_ta = ta_bit;
	    m_ma = ma_bit;
	    m_ratio = calculate_ratio (m_sew, m_vlmul);
	    m_max_sew = get_max_int_sew ();
	    normalize_demand (demand_flags::DEMAND_AVL_P
			      | demand_flags::DEMAND_RATIO_P);
	  }
	else
	  set_unknown ();
	m_lambda = lambda_state::make_defined_runtime ();
	return;
      }

    /* `@read_vtype<mode>': pure vtype READ.  See the matching branch in
       `parse_insn(rtx_insn *)' for why we leave the demand state
       uninitialised instead of marking UNKNOWN.  */
    if (code == CODE_FOR_read_vtypedi || code == CODE_FOR_read_vtypesi)
      {
	m_lambda = lambda_state::make_use_only ();
	return;
      }

    /* IME vsetvl helpers carry a packed IME-demand operand (operand 3).
       Mirror the rtx_insn* variant: expose them as VALID vsetvl sources
       so downstream IME consumers see their demand already satisfied
       and no redundant second IME vsetvl is inserted.  See the
       corresponding branch in `parse_insn(rtx_insn *)' for the layout
       rationale.  */
    if (code == CODE_FOR_vsetvl_imedi || code == CODE_FOR_vsetvl_imesi
	|| code == CODE_FOR_vsetvl_ime_regdi
	|| code == CODE_FOR_vsetvl_ime_regsi)
      {
	extract_insn_cached (insn->rtl ());
	bool is_static_path = (code == CODE_FOR_vsetvl_imedi
			       || code == CODE_FOR_vsetvl_imesi);
	HOST_WIDE_INT ime_pack = INTVAL (recog_data.operand[3]);
	ime_vtype_req req = unpack_ime_req (ime_pack);
	bool have_ime_demand = (req.sew != 0);

	uint8_t sew = 0;
	vlmul_type vlmul = LMUL_RESERVED;
	bool ta_bit = true, ma_bit = true;

	if (is_static_path)
	  {
	    HOST_WIDE_INT vtype_const = INTVAL (recog_data.operand[2]);
	    if (!decode_static_vtype_const (vtype_const, &sew, &vlmul,
					    &ta_bit, &ma_bit))
	      {
		set_unknown ();
		m_lambda = lambda_state::make_killed ();
		return;
	      }
	  }
	else if (have_ime_demand)
	  {
	    sew = req.sew;
	    vlmul = (vlmul_type) req.lmul;
	    ta_bit = ma_bit = true;
	  }
	else
	  {
	    set_unknown ();
	    m_lambda = lambda_state::make_killed ();
	    return;
	  }

	set_valid ();
	m_avl = recog_data.operand[1];
	m_vl = recog_data.operand[0];
	dest_vl = m_vl;
	m_sew = sew;
	m_vlmul = vlmul;
	m_ta = ta_bit;
	m_ma = ma_bit;
	m_ratio = calculate_ratio (m_sew, m_vlmul);
	m_max_sew = get_max_int_sew ();
	unsigned dflags = demand_flags::DEMAND_AVL_P;
	if (have_ime_demand)
	  {
	    m_ime_required = true;
	    m_ime_req = req;
	    m_vlmul = (vlmul_type) m_ime_req.lmul;
	    m_ratio = calculate_ratio (m_sew, m_vlmul);
	    dflags |= demand_flags::DEMAND_SEW_P
		      | demand_flags::DEMAND_LMUL_P
		      | demand_flags::DEMAND_IME_P;
	  }
	else
	  dflags |= demand_flags::DEMAND_RATIO_P;
	normalize_demand (dflags);
	m_lambda = classify_lambda_effect (insn->rtl ());
	return;
      }

    /* `riscv_vsetlambda_install' is special: it issues a register-form
       `vsetvl x0, x0, t' where t is constructed from the current vtype
       with ONLY the lambda field modified.  All other vtype bits
       (SEW, LMUL, ratio, ta, ma) are preserved by construction; in
       SEW/LMUL/policy/AVL demand analysis the install is transparent.

       If we called `set_unknown()' here (as the bug originally did),
       the demand chain from the upstream vsetvli would break and the
       pass would re-insert a vsetvli before the next consumer -- and
       that re-inserted vsetvli would WARL-clobber the lambda we just
       established.

       Leave m_state UNINITIALIZED so the demand analysis treats this
       insn as a no-op for (SEW, LMUL, ratio, ta, ma); the lambda
       lattice in `m_lambda' tracks the actual DEF of vtype.lambda.  */
    if (code == CODE_FOR_riscv_vsetlambda_installdi
	|| code == CODE_FOR_riscv_vsetlambda_installsi)
      {
	m_lambda = classify_lambda_effect (insn->rtl ());
	return;
      }

    /* If this is something that updates VL/VTYPE that we don't know about, set
       the state to unknown.  */
    if (!vector_config_insn_p (insn->rtl ()) && !has_vtype_op (insn->rtl ())
	&& (find_access (insn->defs (), VL_REGNUM)
	    || find_access (insn->defs (), VTYPE_REGNUM)))
      {
	set_unknown ();
	/* Any opaque vtype writer kills the tracked lambda value.  */
	if (find_access (insn->defs (), VTYPE_REGNUM))
	  m_lambda = lambda_state::make_killed ();
	return;
      }

    if (!vector_config_insn_p (insn->rtl ()) && !has_vtype_op (insn->rtl ()))
      /* uninitialized */
      return;

    set_valid ();

    m_avl = ::get_avl (insn->rtl ());
    if (m_avl)
      {
	if (vsetvl_insn_p (insn->rtl ()) || has_vlmax_avl ())
	  {
	    m_vl = ::get_vl (insn->rtl ());
	    dest_vl = m_vl;
	  }

	if (has_nonvlmax_reg_avl ())
	  m_avl_def = find_access (insn->uses (), REGNO (m_avl))->def ();
      }

    m_sew = ::get_sew (insn->rtl ());
    m_vlmul = ::get_vlmul (insn->rtl ());
    m_ratio = get_attr_ratio (insn->rtl ());
    /* For IME insns the physically encoded vtype.LMUL is the *input*
       LMUL, not the accumulator's mode LMUL that `::get_vlmul' just
       returned.  Specifically for the W=1 mmacc patterns the
       accumulator is m16, and `calculate_ratio (sew, LMUL_16)' is not
       defined -- the IME vsetvl path encodes LMUL_16 only as the
       lambda-width selector, never as a SEW/LMUL ratio.  The IME
       branch below rewrites both `m_vlmul' and `m_ratio' to the input
       LMUL, so simply skip the standard ratio fallback when the insn
       carries `requires_ime_vtype'; without this gate the m16 mmacc
       patterns would ICE here before the IME branch ever runs.  */
    bool ime_required_p
      = (get_attr_requires_ime_vtype (insn->rtl ())
	 == REQUIRES_IME_VTYPE_YES);
    if (m_ratio == INVALID_ATTRIBUTE && !ime_required_p)
      /* when get_attr_ratio is invalid, this kind of instructions
	 doesn't care about ratio. However, we still need this value
	 in demand info backward analysis.  */
      m_ratio = calculate_ratio (m_sew, m_vlmul);
    m_ta = tail_agnostic_p (insn->rtl ());
    m_ma = mask_agnostic_p (insn->rtl ());

    if (ime_required_p)
      {
	m_ime_required = true;
	m_ime_req = riscv_extract_ime_requirements (insn->rtl ());
	/* For an IME mmacc, the physically encoded vtype.LMUL is the
	   *input* LMUL (m_ime_req.lmul), not the data-mode LMUL that
	   ::get_vlmul returned above (which is the accumulator's MUL_C
	   for widening variants).  Sync m_vlmul/m_ratio with what the
	   vsetvl_ime emission actually writes, so sew_lmul compatibility
	   checks against a subsequent non-IME consumer (e.g. vmts.v
	   demanding VLMUL=m4 after a widening vfwmmacc that left
	   vtype.LMUL=m1) correctly observe the mismatch and emit a
	   vtype change.  Non-widening mmacc has input LMUL == acc LMUL
	   so the override is a no-op there.  */
	m_vlmul = (vlmul_type) m_ime_req.lmul;
	m_ratio = calculate_ratio (m_sew, m_vlmul);
      }

    m_lambda = classify_lambda_effect (insn->rtl ());

    /* If merge operand is undef value, we prefer agnostic.  */
    int merge_op_idx = get_attr_merge_op_idx (insn->rtl ());
    if (merge_op_idx != INVALID_ATTRIBUTE
	&& satisfies_constraint_vu (recog_data.operand[merge_op_idx]))
      {
	m_ta = true;
	m_ma = true;
      }

    /* Determine the demand info of the RVV insn.  */
    m_max_sew = get_max_int_sew ();
    unsigned dflags = 0;
    if (vector_config_insn_p (insn->rtl ()))
      {
	dflags |= demand_flags::DEMAND_AVL_P;
	dflags |= demand_flags::DEMAND_RATIO_P;
      }
    else
      {
	if (has_vl_op (insn->rtl ()))
	  {
	    if (scalar_move_insn_p (insn->rtl ()))
	      {
		/* If the avl for vmv.s.x comes from the vsetvl instruction, we
		   don't know if the avl is non-zero, so it is set to
		   DEMAND_AVL_P for now. it may be corrected to
		   DEMAND_NON_ZERO_AVL_P later when more information is
		   available.
		 */
		if (has_non_zero_avl ())
		  dflags |= demand_flags::DEMAND_NON_ZERO_AVL_P;
		else
		  dflags |= demand_flags::DEMAND_AVL_P;
	      }
	    else
	      dflags |= demand_flags::DEMAND_AVL_P;
	  }

	/* IME tile load/store ops (vmtl.v, vmts.v, vmtts.v, vmttl.v) take
	   SEW from vtype but their encoded operand width must match.  Two
	   different (SEW, LMUL) pairs with the same ratio are *not*
	   interchangeable for these ops: e8,mf4 has the same ratio=32 as
	   e32,m1 but feeding e8,mf4 into a vmtl.v whose destination is a
	   32-bit element vector register group breaks the tile geometry
	   (hardware derives the in-register stride from vtype.SEW; with a
	   mismatch the load reads at byte stride into a 32-bit element
	   register and the data layout is wrong, and on QEMU the IME
	   helper SIGFPEs from a degenerate MUL_C).

	   Force exact (SEW, LMUL) demand for these ops instead of the
	   default ratio-only demand that get_attr_ratio's mode-based
	   default would otherwise produce.  */
	bool ime_tile_op_p = (get_attr_type (insn->rtl ()) == TYPE_VLDS
			      || get_attr_type (insn->rtl ()) == TYPE_VSTS)
			     && insn_uses_lambda_p (insn->rtl ());

	if (!ime_tile_op_p
	    && get_attr_ratio (insn->rtl ()) != INVALID_ATTRIBUTE)
	  dflags |= demand_flags::DEMAND_RATIO_P;
	else
	  {
	    if (scalar_move_insn_p (insn->rtl ()) && m_ta)
	      {
		dflags |= demand_flags::DEMAND_GE_SEW_P;
		m_max_sew = get_attr_type (insn->rtl ()) == TYPE_VFMOVFV
			      ? get_max_float_sew ()
			      : get_max_int_sew ();
	      }
	    else
	      dflags |= demand_flags::DEMAND_SEW_P;

	    if (!ignore_vlmul_insn_p (insn->rtl ()))
	      dflags |= demand_flags::DEMAND_LMUL_P;
	  }

	/* Demand policy for agnostic if the uarch has a preference.  */
	if (!m_ta || riscv_prefer_agnostic_p ())
	  dflags |= demand_flags::DEMAND_TAIL_POLICY_P;
	if (!m_ma || riscv_prefer_agnostic_p ())
	  dflags |= demand_flags::DEMAND_MASK_POLICY_P;
      }

    if (m_ime_required)
      dflags |= demand_flags::DEMAND_IME_P;

    normalize_demand (dflags);

    /* Optimize AVL from the vsetvl instruction.  */
    insn_info *def_insn = extract_single_source (get_avl_def ());
    if (def_insn && vsetvl_insn_p (def_insn->rtl ()))
      {
	vsetvl_info def_info = vsetvl_info (def_insn);
	if ((scalar_move_insn_p (insn->rtl ())
	     || def_info.get_ratio () == get_ratio ())
	    && (def_info.has_vlmax_avl () || def_info.has_imm_avl ()))
	  {
	    update_avl (def_info);
	    if (scalar_move_insn_p (insn->rtl ()) && has_non_zero_avl ())
	      m_avl_demand = avl_demand_type::non_zero_avl;
	  }
      }

    /* Determine if dest operand(vl) has been used by non-RVV instructions.  */
    if (dest_vl)
      m_vl_used_by_non_rvv_insn = reg_used (get_insn (), dest_vl);

    /* Collect the read vl insn for the fault-only-first rvv loads.  */
    if (fault_first_load_p (insn->rtl ()))
      {
	for (insn_info *i = insn->next_nondebug_insn ();
	     i && i->bb () == insn->bb (); i = i->next_nondebug_insn ())
	  {
	    if (find_access (i->defs (), VL_REGNUM))
	      break;
	    if (i->rtl () && read_vl_insn_p (i->rtl ()))
	      {
		m_read_vl_insn = i;
		break;
	      }
	  }
	/* If no csrr found but this is a _set_vl style fault-only-first
	   load, use the insn itself as the VL source.
	   If we have two identical vector configs that just differ in
	   AVL and the AVL is just "modified" by a read_vl we
	   can consider them equal and elide the second one.  */
	if (!m_read_vl_insn && get_fof_set_vl_reg (insn->rtl ()))
	  m_read_vl_insn = insn;
      }
  }

  /* Returns the corresponding vsetvl rtx pat.  */
  rtx get_vsetvl_pat (bool ignore_vl = false) const
  {
    rtx avl = get_avl ();
    /* if optimization == 0 and the instruction is vmv.x.s/vfmv.f.s,
       set the value of avl to (const_int 0) so that VSETVL PASS will
       insert vsetvl correctly.*/
    if (!get_avl ())
      avl = GEN_INT (0);
    rtx sew = gen_int_mode (get_sew (), Pmode);
    rtx vlmul = gen_int_mode (get_vlmul (), Pmode);
    rtx ta = gen_int_mode (get_ta (), Pmode);
    rtx ma = gen_int_mode (get_ma (), Pmode);

    if (change_vtype_only_p ())
      return gen_vsetvl_vtype_change_only (sew, vlmul, ta, ma);
    else if (has_vl () && !ignore_vl)
      return gen_vsetvl (Pmode, get_vl (), avl, sew, vlmul, ta, ma);
    else
      return gen_vsetvl_discard_result (Pmode, avl, sew, vlmul, ta, ma);
  }

  /* Return true that the non-AVL operands of THIS will be modified
     if we fuse the VL modification from OTHER into THIS.  */
  bool vl_modify_non_avl_op_p (const vsetvl_info &other) const
  {
    /* We don't need to worry about any operands from THIS be
       modified by OTHER vsetvl since we OTHER vsetvl doesn't
       modify any operand.  */
    if (!other.has_vl ())
      return false;

    /* THIS VL operand always preempt OTHER VL operand.  */
    if (this->has_vl ())
      return false;

    /* If THIS has non IMM AVL and THIS is AVL compatible with
       OTHER, the AVL value of THIS is same as VL value of OTHER.  */
    if (!this->has_imm_avl ())
      return false;
    return find_access (this->get_insn ()->uses (), REGNO (other.get_vl ()));
  }

  bool operator== (const vsetvl_info &other) const
  {
    gcc_assert (!uninit_p () && !other.uninit_p ()
		&& "Uninitialization should not happen");

    if (empty_p ())
      return other.empty_p ();
    if (unknown_p ())
      return other.unknown_p ();

    return get_insn () == other.get_insn () && get_bb () == other.get_bb ()
	   && get_avl () == other.get_avl () && get_vl () == other.get_vl ()
	   && get_avl_def () == other.get_avl_def ()
	   && get_sew () == other.get_sew ()
	   && get_vlmul () == other.get_vlmul () && get_ta () == other.get_ta ()
	   && get_ma () == other.get_ma ()
	   && get_avl_demand () == other.get_avl_demand ()
	   && get_sew_lmul_demand () == other.get_sew_lmul_demand ()
	   && get_policy_demand () == other.get_policy_demand ()
	   && get_ime_demand () == other.get_ime_demand ()
	   && ime_required_p () == other.ime_required_p ()
	   && (!ime_required_p () || get_ime_req () == other.get_ime_req ());
  }

  void dump (FILE *file, const char *indent = "") const
  {
    if (uninit_p ())
      {
	fprintf (file, "UNINITIALIZED.\n");
	return;
      }
    else if (unknown_p ())
      {
	fprintf (file, "UNKNOWN.\n");
	return;
      }
    else if (empty_p ())
      {
	fprintf (file, "EMPTY.\n");
	return;
      }
    else if (valid_p ())
      fprintf (file, "VALID (insn %u, bb %u)%s\n", get_insn ()->uid (),
	       get_bb ()->index (), delete_p () ? " (deleted)" : "");
    else
      gcc_unreachable ();

    fprintf (file, "%sDemand fields:", indent);
    if (m_sew_lmul_demand == sew_lmul_demand_type::sew_lmul)
      fprintf (file, " demand_sew_lmul");
    else if (m_sew_lmul_demand == sew_lmul_demand_type::ratio_only)
      fprintf (file, " demand_ratio_only");
    else if (m_sew_lmul_demand == sew_lmul_demand_type::sew_only)
      fprintf (file, " demand_sew_only");
    else if (m_sew_lmul_demand == sew_lmul_demand_type::ge_sew)
      fprintf (file, " demand_ge_sew");
    else if (m_sew_lmul_demand == sew_lmul_demand_type::ratio_and_ge_sew)
      fprintf (file, " demand_ratio_and_ge_sew");

    if (m_policy_demand == policy_demand_type::tail_mask_policy)
      fprintf (file, " demand_tail_mask_policy");
    else if (m_policy_demand == policy_demand_type::tail_policy_only)
      fprintf (file, " demand_tail_policy_only");
    else if (m_policy_demand == policy_demand_type::mask_policy_only)
      fprintf (file, " demand_mask_policy_only");

    if (m_avl_demand == avl_demand_type::avl)
      fprintf (file, " demand_avl");
    else if (m_avl_demand == avl_demand_type::non_zero_avl)
      fprintf (file, " demand_non_zero_avl");

    if (m_ime_demand == ime_demand_type::ime_vtype)
      fprintf (file, " demand_ime_vtype");
    fprintf (file, "\n");

    fprintf (file, "%sSEW=%d, ", indent, get_sew ());
    fprintf (file, "VLMUL=%s, ", vlmul_to_str (get_vlmul ()));
    fprintf (file, "RATIO=%d, ", get_ratio ());
    fprintf (file, "MAX_SEW=%d\n", get_max_sew ());

    fprintf (file, "%sTAIL_POLICY=%s, ", indent, policy_to_str (get_ta ()));
    fprintf (file, "MASK_POLICY=%s\n", policy_to_str (get_ma ()));

    fprintf (file, "%sAVL=", indent);
    print_rtl_single (file, get_avl ());
    fprintf (file, "%sVL=", indent);
    print_rtl_single (file, get_vl ());
    if (change_vtype_only_p ())
      fprintf (file, "%schange vtype only\n", indent);
    if (get_read_vl_insn ())
      fprintf (file, "%sread_vl_insn: insn %u\n", indent,
	       get_read_vl_insn ()->uid ());
    if (vl_used_by_non_rvv_insn_p ())
      fprintf (file, "%suse_by_non_rvv_insn=true\n", indent);
    if (ime_required_p ())
      {
	const ime_vtype_req &r = get_ime_req ();
	fprintf (file,
		 "%sIME: sew=%u mul_c=%s lmul=%u W=%u altfmt_a=%d altfmt_b=%d"
		 " altfmt=%d bs=%d lambda_log2=%d\n",
		 indent, r.sew, vlmul_to_str (r.mul_c), r.lmul, r.widen_w,
		 r.altfmt_a, r.altfmt_b, r.altfmt, r.bs, (int) r.lambda_log2);
      }
  }
};

/* Encode the vsew field (bits 3..5 of vtype) from an element width in
   bits.  SEW_BITS must be one of {8, 16, 32, 64}.  */

static unsigned
ime_vsew_encoding (int sew_bits)
{
  /* e8 -> 000, e16 -> 001, e32 -> 010, e64 -> 011.  */
  gcc_assert (sew_bits >= 8 && (sew_bits & (sew_bits - 1)) == 0);
  return exact_log2 (sew_bits) - 3;
}

/* Compose the compile-time-known portion of the full vtype value:
   vlmul | vsew | vta | vma | altfmt | bs | altfmt_B | altfmt_A
   (i.e. everything except the lambda field at bits XLEN-4..XLEN-2,
   which may need runtime computation).  */

static HOST_WIDE_INT
compute_static_vtype_without_lambda (const vsetvl_info &info, int xlen)
{
  HOST_WIDE_INT val = 0;
  const ime_vtype_req &r = info.get_ime_req ();

  /* The vtype LMUL field encodes the source register-group size for IME
     mmacc instructions (i.e. the vs1/vs2 LMUL), NOT the accumulator's
     MUL_C.  MUL_C is derived in hardware from VLEN/(SEW*lambda^2) and
     is independent of LMUL -- see IME spec section "Integer matrix
     multiply-accumulate".  For an intrinsic like
     __riscv_vmmacc_vv_i32m2 with m1 sources (default LMUL=1), vtype
     must carry LMUL=m1 even though the accumulator type is m2.  For
     non-IME and for tile load/store, info.get_vlmul() already returns
     the correct LMUL (the data vector's LMUL).  */
  /* LMUL_16 is a compiler-internal value with no architectural vtype
     encoding -- it must never reach the encoding path.  IME mmacc with
     an m16 accumulator always uses the input LMUL (m1..m8) for
     vtype.LMUL.  */
  if (info.ime_required_p ())
    {
      gcc_assert (r.lmul != LMUL_16);
      val |= (unsigned) r.lmul;
    }
  else
    {
      gcc_assert (info.get_vlmul () != LMUL_16);
      val |= (unsigned) info.get_vlmul ();
    }
  val |= (HOST_WIDE_INT) ime_vsew_encoding (info.get_sew ()) << 3;
  if (info.get_ta ())
    val |= (HOST_WIDE_INT) 1 << 6;
  if (info.get_ma ())
    val |= (HOST_WIDE_INT) 1 << 7;

  if (r.altfmt)
    val |= (HOST_WIDE_INT) 1 << 8;
  if (r.bs)
    val |= (HOST_WIDE_INT) 1 << (xlen - 5);
  if (r.altfmt_a)
    val |= (HOST_WIDE_INT) 1 << (xlen - 6);
  if (r.altfmt_b)
    val |= (HOST_WIDE_INT) 1 << (xlen - 7);
  return val;
}

/* Return a const_int rtx holding the full vtype value for an IME
   instruction, or NULL_RTX if the value cannot be computed at compile
   time (runtime lambda dispatch required -- NYI).

   The vsetvl pass runs post-reload and cannot create fresh pseudos,
   so the IME setup pattern (@vsetvl_ime<mode>) takes a const_int
   vtype operand and synthesises the load in its own asm template.
   This helper's job is to compute that constant.  Runtime lambda
   support will be added once we have a post-reload-safe way to emit
   the csrr/ctz sequence.  */

static rtx
emit_ime_vtype_build (const vsetvl_info &info)
{
  const ime_vtype_req &r = info.get_ime_req ();
  int xlen = GET_MODE_BITSIZE (Pmode);
  HOST_WIDE_INT static_val = compute_static_vtype_without_lambda (info, xlen);

  if (r.lambda_log2 == -2)
    {
      sorry ("IME intrinsic: the %<(VLEN, SEW, MUL_C)%> combination does "
	     "not yield a supported %<lambda%> value");
      return NULL_RTX;
    }

  if (r.lambda_log2 >= 0)
    {
      int lambda_val = 1 << r.lambda_log2;
      if (!riscv_ime_lambda_supported_p (lambda_val))
	{
	  error ("IME intrinsic requires %<lambda=%d%> which is not in the "
		 "set allowed by %<-mrvv-ime-lambda=%>", lambda_val);
	  return NULL_RTX;
	}
      /* Static lambda: the full vtype is a compile-time constant.  */
      int lambda_enc = r.lambda_log2 + 1;
      static_val |= (HOST_WIDE_INT) lambda_enc << (xlen - 4);
      return gen_int_mode (static_val, Pmode);
    }

  /* Runtime lambda: the intrinsic expander (pre-reload) has already
     emitted the multi-instruction vtype setup sequence (csrr vlenb +
     ctz + shifts + vsetvl + WARL check).  Return NULL_RTX so the
     vsetvl pass skips its own insertion for this instruction.  */
  return NULL_RTX;
}

class vsetvl_block_info
{
public:
  /* The static execute probability of the demand info.  */
  profile_probability probability;

  auto_vec<vsetvl_info> local_infos;
  vsetvl_info global_info;
  bb_info *bb;

  vsetvl_block_info () : bb (nullptr)
  {
    local_infos.safe_grow_cleared (0);
    global_info.set_empty ();
  }
  vsetvl_block_info (const vsetvl_block_info &other)
    : probability (other.probability), local_infos (other.local_infos.copy ()),
      global_info (other.global_info), bb (other.bb)
  {}

  vsetvl_info &get_entry_info ()
  {
    gcc_assert (!empty_p ());
    return local_infos.is_empty () ? global_info : local_infos[0];
  }
  vsetvl_info &get_exit_info ()
  {
    gcc_assert (!empty_p ());
    return local_infos.is_empty () ? global_info
				   : local_infos[local_infos.length () - 1];
  }
  const vsetvl_info &get_entry_info () const
  {
    gcc_assert (!empty_p ());
    return local_infos.is_empty () ? global_info : local_infos[0];
  }
  const vsetvl_info &get_exit_info () const
  {
    gcc_assert (!empty_p ());
    return local_infos.is_empty () ? global_info
				   : local_infos[local_infos.length () - 1];
  }

  bool empty_p () const { return local_infos.is_empty () && !has_info (); }
  bool has_info () const { return !global_info.empty_p (); }
  void set_info (const vsetvl_info &info)
  {
    gcc_assert (local_infos.is_empty ());
    global_info = info;
    global_info.set_bb (bb);
  }
  void set_empty_info () { global_info.set_empty (); }
};

/* Same as REG_USED () but looks for a single use in an RVV insn's AVL
   operand.  */
static bool
reg_single_use_in_avl (insn_info *insn, rtx reg)
{
  if (!reg)
    return false;
  unsigned int regno = REGNO (reg);
  const hash_set<use_info *> vl_uses = get_all_real_uses (insn, regno);
  for (use_info *use : vl_uses)
    {
      gcc_assert (use->insn ()->is_real ());
      rtx_insn *rinsn = use->insn ()->rtl ();
      if (!has_vl_op (rinsn)
	  || count_regno_occurrences (rinsn, regno) != 1)
	return false;

      vsetvl_info info = vsetvl_info (use->insn ());

      if (!info.has_nonvlmax_reg_avl ())
	return false;

      rtx avl = info.get_avl ();
      if (avl && REG_P (avl) && regno == REGNO (avl))
	return true;
    }
  return false;
}

/* Demand system is the RVV-based VSETVL info analysis tools wrapper.
   It defines compatible rules for SEW/LMUL, POLICY and AVL.
   Also, it provides 3 interfaces available_p, compatible_p and
   merge for the VSETVL PASS analysis and optimization.

     - available_p: Determine whether the next info can get the
       available VSETVL status from previous info.
       e.g. bb 2 (demand SEW = 32, LMUL = M2) -> bb 3 (demand RATIO = 16).
       Since bb 2 demand info (SEW/LMUL = 32/2 = 16) satisfies the bb 3
       demand, the VSETVL instruction in bb 3 can be elided.
       available_p (previous, next) is true in such situation.
     - compatible_p: Determine whether prev_info is compatible with next_info
       so that we can have a new merged info that is available to both of them.
     - merge: Merge the stricter demand information from
       next_info into prev_info so that prev_info becomes available to
       next_info.  */
class demand_system
{
private:
  /* predictors.  */

  inline bool always_true (const vsetvl_info &prev ATTRIBUTE_UNUSED,
			   const vsetvl_info &next ATTRIBUTE_UNUSED)
  {
    return true;
  }
  inline bool always_false (const vsetvl_info &prev ATTRIBUTE_UNUSED,
			    const vsetvl_info &next ATTRIBUTE_UNUSED)
  {
    return false;
  }

  /* predictors for sew and lmul */

  inline bool lmul_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.get_vlmul () == next.get_vlmul ();
  }
  inline bool sew_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.get_sew () == next.get_sew ();
  }
  inline bool sew_lmul_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return lmul_eq_p (prev, next) && sew_eq_p (prev, next);
  }
  inline bool sew_ge_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.get_sew () == next.get_sew ()
	   || (next.get_ta () && prev.get_sew () > next.get_sew ());
  }
  inline bool sew_le_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.get_sew () == next.get_sew ()
	   || (prev.get_ta () && prev.get_sew () < next.get_sew ());
  }
  inline bool prev_sew_le_next_max_sew_p (const vsetvl_info &prev,
					  const vsetvl_info &next)
  {
    return prev.get_sew () <= next.get_max_sew ();
  }
  inline bool next_sew_le_prev_max_sew_p (const vsetvl_info &prev,
					  const vsetvl_info &next)
  {
    return next.get_sew () <= prev.get_max_sew ();
  }
  inline bool max_sew_overlap_p (const vsetvl_info &prev,
				 const vsetvl_info &next)
  {
    return !(prev.get_sew () > next.get_max_sew ()
	     || next.get_sew () > prev.get_max_sew ());
  }
  inline bool ratio_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    return prev.has_same_ratio (next);
  }
  inline bool prev_ratio_valid_for_next_sew_p (const vsetvl_info &prev,
					       const vsetvl_info &next)
  {
    return prev.get_ratio () >= (next.get_sew () / MAX_LMUL);
  }
  inline bool next_ratio_valid_for_prev_sew_p (const vsetvl_info &prev,
					       const vsetvl_info &next)
  {
    return next.get_ratio () >= (prev.get_sew () / MAX_LMUL);
  }
  inline bool sew_ge_and_ratio_eq_p (const vsetvl_info &prev,
				     const vsetvl_info &next)
  {
    return sew_ge_p (prev, next) && ratio_eq_p (prev, next);
  }
  inline bool sew_ge_and_prev_sew_le_next_max_sew_p (const vsetvl_info &prev,
						     const vsetvl_info &next)
  {
    return sew_ge_p (prev, next) && prev_sew_le_next_max_sew_p (prev, next);
  }
  inline bool
  sew_ge_and_prev_sew_le_next_max_sew_and_next_ratio_valid_for_prev_sew_p (
    const vsetvl_info &prev, const vsetvl_info &next)
  {
    return sew_ge_p (prev, next) && prev_sew_le_next_max_sew_p (prev, next)
	   && next_ratio_valid_for_prev_sew_p (prev, next);
  }
  inline bool
  sew_ge_and_prev_sew_le_next_max_sew_and_ratio_eq_p (
    const vsetvl_info &prev, const vsetvl_info &next)
  {
    return sew_ge_p (prev, next) && prev_sew_le_next_max_sew_p (prev, next)
	   && ratio_eq_p (prev, next);
  }
  inline bool sew_le_and_next_sew_le_prev_max_sew_p (const vsetvl_info &prev,
						     const vsetvl_info &next)
  {
    return sew_le_p (prev, next) && next_sew_le_prev_max_sew_p (prev, next);
  }
  inline bool
  max_sew_overlap_and_next_ratio_valid_for_prev_sew_p (const vsetvl_info &prev,
						       const vsetvl_info &next)
  {
    if (next_ratio_valid_for_prev_sew_p (prev, next)
	&& max_sew_overlap_p (prev, next))
      {
	if (next.get_sew () < prev.get_sew ()
	    && (!next.get_ta () || !next.get_ma ()))
	  return false;
	return true;
      }
    return false;
  }
  inline bool
  sew_le_and_next_sew_le_prev_max_sew_and_ratio_eq_p (const vsetvl_info &prev,
						      const vsetvl_info &next)
  {
    return sew_le_p (prev, next) && ratio_eq_p (prev, next)
	   && next_sew_le_prev_max_sew_p (prev, next);
  }
  inline bool
  max_sew_overlap_and_prev_ratio_valid_for_next_sew_p (const vsetvl_info &prev,
						       const vsetvl_info &next)
  {
    return prev_ratio_valid_for_next_sew_p (prev, next)
	   && max_sew_overlap_p (prev, next);
  }
  inline bool
  sew_le_and_next_sew_le_prev_max_sew_and_prev_ratio_valid_for_next_sew_p (
    const vsetvl_info &prev, const vsetvl_info &next)
  {
    return sew_le_p (prev, next) && prev_ratio_valid_for_next_sew_p (prev, next)
	   && next_sew_le_prev_max_sew_p (prev, next);
  }
  inline bool max_sew_overlap_and_ratio_eq_p (const vsetvl_info &prev,
					      const vsetvl_info &next)
  {
    return ratio_eq_p (prev, next) && max_sew_overlap_p (prev, next);
  }

  /* predictors for tail and mask policy */

  inline bool tail_policy_eq_p (const vsetvl_info &prev,
				const vsetvl_info &next)
  {
    return prev.get_ta () == next.get_ta ();
  }
  inline bool mask_policy_eq_p (const vsetvl_info &prev,
				const vsetvl_info &next)
  {
    return prev.get_ma () == next.get_ma ();
  }
  inline bool tail_mask_policy_eq_p (const vsetvl_info &prev,
				     const vsetvl_info &next)
  {
    return tail_policy_eq_p (prev, next) && mask_policy_eq_p (prev, next);
  }

  /* predictors for avl */

  inline bool modify_or_use_vl_p (insn_info *i, const vsetvl_info &info)
  {
    if (info.has_vl ())
      {
	if (find_access (i->defs (), REGNO (info.get_vl ())))
	  return true;
	if (find_access (i->uses (), REGNO (info.get_vl ())))
	  {
	    resource_info resource = full_register (REGNO (info.get_vl ()));
	    def_lookup dl1 = crtl->ssa->find_def (resource, i);
	    def_lookup dl2 = crtl->ssa->find_def (resource, info.get_insn ());
	    if (dl1.matching_set () || dl2.matching_set ())
	      return true;
	    /* If their VLs are coming from same def, we still want to fuse
	       their VSETVL demand info to gain better performance.  */
	    return dl1.prev_def (i) != dl2.prev_def (i);
	  }
      }
    return false;
  }
  inline bool modify_avl_p (insn_info *i, const vsetvl_info &info)
  {
    return info.has_nonvlmax_reg_avl ()
	   && find_access (i->defs (), REGNO (info.get_avl ()));
  }

  inline bool modify_reg_between_p (insn_info *prev_insn, insn_info *curr_insn,
				    unsigned regno)
  {
    gcc_assert (prev_insn->compare_with (curr_insn) < 0);
    for (insn_info *i = curr_insn->prev_nondebug_insn (); i != prev_insn;
	 i = i->prev_nondebug_insn ())
      {
	// no def of regno
	if (find_access (i->defs (), regno))
	  return true;
      }
    return false;
  }

  inline bool reg_avl_equal_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    if (!prev.has_nonvlmax_reg_avl () || !next.has_nonvlmax_reg_avl ())
      return false;

    if (same_equiv_note_p (prev.get_avl_def (), next.get_avl_def ()))
      return true;

    if (REGNO (prev.get_avl ()) != REGNO (next.get_avl ()))
      return false;

    insn_info *prev_insn = prev.get_insn ();
    if (prev.get_bb () != prev_insn->bb ())
      prev_insn = prev.get_bb ()->end_insn ();

    insn_info *next_insn = next.get_insn ();
    if (next.get_bb () != next_insn->bb ())
      next_insn = next.get_bb ()->end_insn ();

    return avl_vl_unmodified_between_p (prev_insn, next_insn, next, false);
  }

  inline bool avl_equal_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());

    if (next.has_vl () && next.vl_used_by_non_rvv_insn_p ())
      return false;

    if (vector_config_insn_p (prev.get_insn ()->rtl ()) && next.get_avl_def ()
	&& next.get_avl_def ()->insn () == prev.get_insn ())
      return true;

    if (prev.get_read_vl_insn ())
      {
	if (!next.has_nonvlmax_reg_avl () || !next.get_avl_def ())
	  return false;
	insn_info *avl_def_insn = extract_single_source (next.get_avl_def ());
	return avl_def_insn == prev.get_read_vl_insn ();
      }

    if (prev == next && prev.has_nonvlmax_reg_avl ())
      {
	insn_info *insn = prev.get_insn ();
	bb_info *bb = insn->bb ();
	for (insn_info *i = insn; real_insn_and_same_bb_p (i, bb);
	     i = i->next_nondebug_insn ())
	  if (find_access (i->defs (), REGNO (prev.get_avl ())))
	    return false;
      }

    if (prev.has_vlmax_avl () && next.has_vlmax_avl ())
      return true;
    else if (prev.has_imm_avl () && next.has_imm_avl ())
      return INTVAL (prev.get_avl ()) == INTVAL (next.get_avl ());
    else if (prev.has_vl () && next.has_nonvlmax_reg_avl ()
	     && REGNO (prev.get_vl ()) == REGNO (next.get_avl ()))
      {
	insn_info *prev_insn = prev.insn_inside_bb_p ()
				 ? prev.get_insn ()
				 : prev.get_bb ()->end_insn ();

	insn_info *next_insn = next.insn_inside_bb_p ()
				 ? next.get_insn ()
				 : next.get_bb ()->end_insn ();
	return avl_vl_unmodified_between_p (prev_insn, next_insn, next, false);
      }
    else if (prev.has_nonvlmax_reg_avl () && next.has_nonvlmax_reg_avl ())
      return reg_avl_equal_p (prev, next);

    return false;
  }
  inline bool avl_equal_or_prev_avl_non_zero_p (const vsetvl_info &prev,
						const vsetvl_info &next)
  {
    return avl_equal_p (prev, next) || prev.has_non_zero_avl ();
  }

  inline bool can_use_next_avl_p (const vsetvl_info &prev,
				  const vsetvl_info &next)
  {
    /* Forbid the AVL/VL propagation if VL of NEXT is used
       by non-RVV instructions.  This is because:

	 bb 2:
	   PREV: scalar move (no AVL)
	 bb 3:
	   NEXT: vsetvl a5(VL), a4(AVL) ...
	   branch a5,zero

       Since user vsetvl instruction is no side effect instruction
       which should be placed in the correct and optimal location
       of the program by the previous PASS, it is unreasonable that
       VSETVL PASS tries to move it to another places if it used by
       non-RVV instructions.

       Note: We only forbid the cases that VL is used by the following
       non-RVV instructions which will cause issues.  We don't forbid
       other cases since it won't cause correctness issues and we still
       more demand info are fused backward.  The later LCM algorithm
       should know the optimal location of the vsetvl.  */
    if (next.has_vl () && next.vl_used_by_non_rvv_insn_p ())
      return false;

    if (!next.has_nonvlmax_reg_avl () && !next.has_vl ())
      return true;

    insn_info *prev_insn = prev.get_insn ();
    if (prev.get_bb () != prev_insn->bb ())
      prev_insn = prev.get_bb ()->end_insn ();

    insn_info *next_insn = next.get_insn ();
    if (next.get_bb () != next_insn->bb ())
      next_insn = next.get_bb ()->end_insn ();

    return avl_vl_unmodified_between_p (prev_insn, next_insn, next);
  }

  inline bool avl_equal_or_next_avl_non_zero_and_can_use_next_avl_p (
    const vsetvl_info &prev, const vsetvl_info &next)
  {
    return avl_equal_p (prev, next)
	   || (next.has_non_zero_avl () && can_use_next_avl_p (prev, next));
  }

  /* modifiers  */

  inline void nop (const vsetvl_info &prev ATTRIBUTE_UNUSED,
		   const vsetvl_info &next ATTRIBUTE_UNUSED)
  {}

  /* modifiers for sew and lmul */

  inline void use_min_of_max_sew (vsetvl_info &prev, const vsetvl_info &next)
  {
    prev.set_max_sew (MIN (prev.get_max_sew (), next.get_max_sew ()));
  }
  inline void use_next_sew (vsetvl_info &prev, const vsetvl_info &next)
  {
    prev.set_sew (next.get_sew ());
    use_min_of_max_sew (prev, next);
  }
  inline void use_max_sew (vsetvl_info &prev, const vsetvl_info &next)
  {
    bool prev_sew_larger = prev.get_sew () >= next.get_sew ();
    const vsetvl_info from = prev_sew_larger ? prev : next;
    prev.set_sew (from.get_sew ());
    prev.set_vlmul (from.get_vlmul ());
    prev.set_ratio (from.get_ratio ());
    use_min_of_max_sew (prev, next);
  }
  inline void use_next_sew_lmul (vsetvl_info &prev, const vsetvl_info &next)
  {
    use_next_sew (prev, next);
    prev.set_vlmul (next.get_vlmul ());
    prev.set_ratio (next.get_ratio ());
  }
  inline void use_next_sew_with_prev_ratio (vsetvl_info &prev,
					    const vsetvl_info &next)
  {
    use_next_sew (prev, next);
    prev.set_vlmul (calculate_vlmul (next.get_sew (), prev.get_ratio ()));
  }
  inline void modify_lmul_with_next_ratio (vsetvl_info &prev,
					   const vsetvl_info &next)
  {
    prev.set_vlmul (calculate_vlmul (prev.get_sew (), next.get_ratio ()));
    prev.set_ratio (next.get_ratio ());
  }

  inline void use_max_sew_and_lmul_with_next_ratio (vsetvl_info &prev,
						    const vsetvl_info &next)
  {
    int max_sew = MAX (prev.get_sew (), next.get_sew ());
    prev.set_vlmul (calculate_vlmul (max_sew, next.get_ratio ()));
    use_max_sew (prev, next);
    prev.set_ratio (next.get_ratio ());
  }

  inline void use_max_sew_and_lmul_with_prev_ratio (vsetvl_info &prev,
						    const vsetvl_info &next)
  {
    int max_sew = MAX (prev.get_sew (), next.get_sew ());
    prev.set_vlmul (calculate_vlmul (max_sew, prev.get_ratio ()));
    prev.set_sew (max_sew);
  }

  /* modifiers for tail and mask policy */

  inline void use_tail_policy (vsetvl_info &prev, const vsetvl_info &next)
  {
    if (!next.get_ta ())
      prev.set_ta (next.get_ta ());
  }
  inline void use_mask_policy (vsetvl_info &prev, const vsetvl_info &next)
  {
    if (!next.get_ma ())
      prev.set_ma (next.get_ma ());
  }
  inline void use_tail_mask_policy (vsetvl_info &prev, const vsetvl_info &next)
  {
    use_tail_policy (prev, next);
    use_mask_policy (prev, next);
  }

  /* modifiers for avl */

  inline void use_next_avl (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (can_use_next_avl_p (prev, next));
    prev.update_avl (next);
  }

  inline void use_next_avl_when_not_equal (vsetvl_info &prev,
					   const vsetvl_info &next)
  {
    if (avl_equal_p (prev, next))
      return;
    gcc_assert (next.has_non_zero_avl ());
    use_next_avl (prev, next);
  }

  /* predictors for IME vtype dimension */

  inline bool ime_eq_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    /* Only called via the (ime_vtype, ime_vtype) rule, where both sides
       have m_ime_required=true and a populated m_ime_req.  */
    return prev.get_ime_req () == next.get_ime_req ();
  }

  /* modifier for IME vtype dimension: copy next's IME state into prev.  */

  inline void use_next_ime (vsetvl_info &prev, const vsetvl_info &next)
  {
    prev.set_ime_required (next.ime_required_p ());
    prev.set_ime_req (next.get_ime_req ());
  }

public:
  /* Can we move vsetvl info between prev_insn and next_insn safe? */
  bool avl_vl_unmodified_between_p (insn_info *prev_insn, insn_info *next_insn,
				    const vsetvl_info &info,
				    bool ignore_vl = false)
  {
    gcc_assert ((ignore_vl && info.has_nonvlmax_reg_avl ())
		|| (info.has_nonvlmax_reg_avl () || info.has_vl ()));

    gcc_assert (!prev_insn->is_debug_insn () && !next_insn->is_debug_insn ());
    if (prev_insn->bb () == next_insn->bb ()
	&& prev_insn->compare_with (next_insn) < 0)
      {
	for (insn_info *i = next_insn->prev_nondebug_insn (); i != prev_insn;
	     i = i->prev_nondebug_insn ())
	  {
	    // no def and use of vl
	    if (!ignore_vl && modify_or_use_vl_p (i, info))
	      return false;

	    // no def of avl
	    if (modify_avl_p (i, info))
	      return false;
	  }
	return true;
      }
    else
      {
	basic_block prev_cfg_bb = prev_insn->bb ()->cfg_bb ();
	if (!ignore_vl && info.has_vl ())
	  {
	    bitmap live_out = df_get_live_out (prev_cfg_bb);
	    if (bitmap_bit_p (live_out, REGNO (info.get_vl ())))
	      return false;
	  }

	/* Find set_info at location of PREV_INSN and NEXT_INSN, Return
	   false if those 2 set_info are different.

	     PREV_INSN --- multiple nested blocks --- NEXT_INSN.

	   Return false if there is any modifications of AVL inside those
	   multiple nested blocks.  */
	if (info.has_nonvlmax_reg_avl ())
	  {
	    resource_info resource = full_register (REGNO (info.get_avl ()));
	    def_lookup dl1 = crtl->ssa->find_def (resource, prev_insn);
	    def_lookup dl2 = crtl->ssa->find_def (resource, next_insn);
	    if (dl2.matching_set ())
	      return false;

	    auto is_phi_or_real
	      = [&] (insn_info *h) { return h->is_real () || h->is_phi (); };

	    def_info *def1 = dl1.matching_set_or_last_def_of_prev_group ();
	    def_info *def2 = dl2.prev_def (next_insn);
	    set_info *set1 = safe_dyn_cast<set_info *> (def1);
	    set_info *set2 = safe_dyn_cast<set_info *> (def2);
	    if (!set1 || !set2)
	      return false;

	    auto is_same_ultimate_def = [&] (set_info *s1, set_info *s2) {
	      return s1->insn ()->is_phi () && s2->insn ()->is_phi ()
		     && look_through_degenerate_phi (s1)
			  == look_through_degenerate_phi (s2);
	    };

	    if (set1 != set2 && !is_same_ultimate_def (set1, set2))
	      {
		if (!is_phi_or_real (set1->insn ())
		    || !is_phi_or_real (set2->insn ()))
		  return false;

		if (set1->insn ()->is_real () && set2->insn ()->is_phi ())
		  {
		    hash_set<set_info *> sets
		      = get_all_sets (set2, true, false, true);
		    if (!sets.contains (set1))
		      return false;
		  }
		else
		  {
		    insn_info *def_insn1 = extract_single_source (set1);
		    insn_info *def_insn2 = extract_single_source (set2);
		    if (!def_insn1 || !def_insn2 || def_insn1 != def_insn2)
		      return false;
		  }
	      }
	  }

	for (insn_info *i = next_insn; i != next_insn->bb ()->head_insn ();
	     i = i->prev_nondebug_insn ())
	  {
	    // no def and use of vl
	    if (!ignore_vl && modify_or_use_vl_p (i, info))
	      return false;

	    // no def of avl
	    if (modify_avl_p (i, info))
	      return false;
	  }

	for (insn_info *i = prev_insn->bb ()->end_insn (); i != prev_insn;
	     i = i->prev_nondebug_insn ())
	  {
	    // no def mad use of vl
	    if (!ignore_vl && modify_or_use_vl_p (i, info))
	      return false;

	    // no def of avl
	    if (modify_avl_p (i, info))
	      return false;
	  }
      }
    return true;
  }

  bool sew_lmul_compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    sew_lmul_demand_type prev_flags = prev.get_sew_lmul_demand ();
    sew_lmul_demand_type next_flags = next.get_sew_lmul_demand ();
#define DEF_SEW_LMUL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,     \
			  AVAILABLE_P, FUSE)                                   \
  if (prev_flags == sew_lmul_demand_type::PREV_FLAGS                           \
      && next_flags == sew_lmul_demand_type::NEXT_FLAGS)                       \
    return COMPATIBLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool sew_lmul_available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    sew_lmul_demand_type prev_flags = prev.get_sew_lmul_demand ();
    sew_lmul_demand_type next_flags = next.get_sew_lmul_demand ();
#define DEF_SEW_LMUL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,     \
			  AVAILABLE_P, FUSE)                                   \
  if (prev_flags == sew_lmul_demand_type::PREV_FLAGS                           \
      && next_flags == sew_lmul_demand_type::NEXT_FLAGS)                       \
    return AVAILABLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  void merge_sew_lmul (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    sew_lmul_demand_type prev_flags = prev.get_sew_lmul_demand ();
    sew_lmul_demand_type next_flags = next.get_sew_lmul_demand ();
#define DEF_SEW_LMUL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,     \
			  AVAILABLE_P, FUSE)                                   \
  if (prev_flags == sew_lmul_demand_type::PREV_FLAGS                           \
      && next_flags == sew_lmul_demand_type::NEXT_FLAGS)                       \
    {                                                                          \
      gcc_assert (COMPATIBLE_P (prev, next));                                  \
      FUSE (prev, next);                                                       \
      prev.set_sew_lmul_demand (sew_lmul_demand_type::NEW_FLAGS);              \
      return;                                                                  \
    }

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool policy_compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    policy_demand_type prev_flags = prev.get_policy_demand ();
    policy_demand_type next_flags = next.get_policy_demand ();
#define DEF_POLICY_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,       \
			AVAILABLE_P, FUSE)                                     \
  if (prev_flags == policy_demand_type::PREV_FLAGS                             \
      && next_flags == policy_demand_type::NEXT_FLAGS)                         \
    return COMPATIBLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool policy_available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    policy_demand_type prev_flags = prev.get_policy_demand ();
    policy_demand_type next_flags = next.get_policy_demand ();
#define DEF_POLICY_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,       \
			AVAILABLE_P, FUSE)                                     \
  if (prev_flags == policy_demand_type::PREV_FLAGS                             \
      && next_flags == policy_demand_type::NEXT_FLAGS)                         \
    return AVAILABLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  void merge_policy (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    policy_demand_type prev_flags = prev.get_policy_demand ();
    policy_demand_type next_flags = next.get_policy_demand ();
#define DEF_POLICY_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,       \
			AVAILABLE_P, FUSE)                                     \
  if (prev_flags == policy_demand_type::PREV_FLAGS                             \
      && next_flags == policy_demand_type::NEXT_FLAGS)                         \
    {                                                                          \
      gcc_assert (COMPATIBLE_P (prev, next));                                  \
      FUSE (prev, next);                                                       \
      prev.set_policy_demand (policy_demand_type::NEW_FLAGS);                  \
      return;                                                                  \
    }

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool vl_not_in_conflict_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    /* We don't fuse this following case:

	li a5, -1
	vmv.s.x v0, a5         -- PREV
	vsetvli a5, ...        -- NEXT

       Don't fuse NEXT into PREV.
    */
    return !prev.vl_modify_non_avl_op_p (next)
	   && !next.vl_modify_non_avl_op_p (prev);
  }

  bool avl_compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    avl_demand_type prev_flags = prev.get_avl_demand ();
    avl_demand_type next_flags = next.get_avl_demand ();
#define DEF_AVL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == avl_demand_type::PREV_FLAGS                                \
      && next_flags == avl_demand_type::NEXT_FLAGS)                            \
    return COMPATIBLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool avl_available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    avl_demand_type prev_flags = prev.get_avl_demand ();
    avl_demand_type next_flags = next.get_avl_demand ();
#define DEF_AVL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == avl_demand_type::PREV_FLAGS                                \
      && next_flags == avl_demand_type::NEXT_FLAGS)                            \
    return AVAILABLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  void merge_avl (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    avl_demand_type prev_flags = prev.get_avl_demand ();
    avl_demand_type next_flags = next.get_avl_demand ();
#define DEF_AVL_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == avl_demand_type::PREV_FLAGS                                \
      && next_flags == avl_demand_type::NEXT_FLAGS)                            \
    {                                                                          \
      gcc_assert (COMPATIBLE_P (prev, next));                                  \
      FUSE (prev, next);                                                       \
      prev.set_avl_demand (avl_demand_type::NEW_FLAGS);                        \
      return;                                                                  \
    }

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool ime_compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    ime_demand_type prev_flags = prev.get_ime_demand ();
    ime_demand_type next_flags = next.get_ime_demand ();
#define DEF_IME_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == ime_demand_type::PREV_FLAGS                                \
      && next_flags == ime_demand_type::NEXT_FLAGS)                            \
    return COMPATIBLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool ime_available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    ime_demand_type prev_flags = prev.get_ime_demand ();
    ime_demand_type next_flags = next.get_ime_demand ();
#define DEF_IME_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == ime_demand_type::PREV_FLAGS                                \
      && next_flags == ime_demand_type::NEXT_FLAGS)                            \
    return AVAILABLE_P (prev, next);

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  void merge_ime (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (prev.valid_p () && next.valid_p ());
    ime_demand_type prev_flags = prev.get_ime_demand ();
    ime_demand_type next_flags = next.get_ime_demand ();
#define DEF_IME_RULE(PREV_FLAGS, NEXT_FLAGS, NEW_FLAGS, COMPATIBLE_P,          \
		     AVAILABLE_P, FUSE)                                        \
  if (prev_flags == ime_demand_type::PREV_FLAGS                                \
      && next_flags == ime_demand_type::NEXT_FLAGS)                            \
    {                                                                          \
      gcc_assert (COMPATIBLE_P (prev, next));                                  \
      FUSE (prev, next);                                                       \
      prev.set_ime_demand (ime_demand_type::NEW_FLAGS);                        \
      return;                                                                  \
    }

#include "riscv-vsetvl.def"

    gcc_unreachable ();
  }

  bool compatible_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    bool compatible_p = sew_lmul_compatible_p (prev, next)
			&& policy_compatible_p (prev, next)
			&& avl_compatible_p (prev, next)
			&& ime_compatible_p (prev, next)
			&& vl_not_in_conflict_p (prev, next);
    return compatible_p;
  }

  bool available_p (const vsetvl_info &prev, const vsetvl_info &next)
  {
    bool available_p = sew_lmul_available_p (prev, next)
		       && policy_available_p (prev, next)
		       && avl_available_p (prev, next)
		       && ime_available_p (prev, next)
		       && vl_not_in_conflict_p (prev, next);
    gcc_assert (!available_p || compatible_p (prev, next));
    return available_p;
  }

  void merge (vsetvl_info &prev, const vsetvl_info &next)
  {
    gcc_assert (compatible_p (prev, next));
    merge_sew_lmul (prev, next);
    merge_policy (prev, next);
    merge_avl (prev, next);
    merge_ime (prev, next);
    gcc_assert (available_p (prev, next));
  }
};


class pre_vsetvl
{
private:
  demand_system m_dem;
  auto_vec<vsetvl_block_info> m_vector_block_infos;

  /* data for avl reaching definition.  */
  sbitmap *m_reg_def_loc;

  /* Holds register uses per basic block.  Restricted to those registers that
     are used as vsetvl destinations.  */
  sbitmap *m_reg_use_loc;

  /* data for vsetvl info reaching definition.  */
  vsetvl_info m_unknown_info;
  auto_vec<vsetvl_info *> m_vsetvl_def_exprs;
  sbitmap *m_vsetvl_def_in;
  sbitmap *m_vsetvl_def_out;

  /* Reaching data for vsetvl AVL operands.  */
  sbitmap *m_vsetvl_avl_reach_in;
  sbitmap *m_vsetvl_avl_reach_out;

  /* data for lcm */
  auto_vec<vsetvl_info *> m_exprs;
  sbitmap *m_avloc;
  sbitmap *m_avin;
  sbitmap *m_avout;
  sbitmap *m_kill;
  sbitmap *m_antloc;
  sbitmap *m_transp;
  sbitmap *m_insert;
  sbitmap *m_del;
  struct edge_list *m_edges;

  auto_vec<vsetvl_info> m_delete_list;

  vsetvl_block_info &get_block_info (const bb_info *bb)
  {
    return m_vector_block_infos[bb->index ()];
  }
  const vsetvl_block_info &get_block_info (const basic_block bb) const
  {
    return m_vector_block_infos[bb->index];
  }

  vsetvl_block_info &get_block_info (const basic_block bb)
  {
    return m_vector_block_infos[bb->index];
  }

  void add_expr (auto_vec<vsetvl_info *> &m_exprs, vsetvl_info &info)
  {
    for (vsetvl_info *item : m_exprs)
      {
	if (*item == info)
	  return;
      }
    m_exprs.safe_push (&info);
  }

  unsigned get_expr_index (auto_vec<vsetvl_info *> &m_exprs,
			   const vsetvl_info &info)
  {
    for (size_t i = 0; i < m_exprs.length (); i += 1)
      {
	if (*m_exprs[i] == info)
	  return i;
      }
    gcc_unreachable ();
  }

  bool anticipated_exp_p (const vsetvl_info &header_info)
  {
    if (!header_info.has_nonvlmax_reg_avl () && !header_info.has_vl ())
      return true;

    bb_info *bb = header_info.get_bb ();
    insn_info *prev_insn = bb->head_insn ();
    insn_info *next_insn = header_info.insn_inside_bb_p ()
			     ? header_info.get_insn ()
			     : header_info.get_bb ()->end_insn ();

    return m_dem.avl_vl_unmodified_between_p (prev_insn, next_insn,
					      header_info);
  }

  bool available_exp_p (const vsetvl_info &prev_info,
			const vsetvl_info &next_info)
  {
    return m_dem.available_p (prev_info, next_info);
  }

  void compute_probabilities ()
  {
    edge e;
    edge_iterator ei;

    for (const bb_info *bb : crtl->ssa->bbs ())
      {
	basic_block cfg_bb = bb->cfg_bb ();
	auto &curr_prob = get_block_info (cfg_bb).probability;

	/* GCC assume entry block (bb 0) are always so
	   executed so set its probability as "always".  */
	if (ENTRY_BLOCK_PTR_FOR_FN (cfun) == cfg_bb)
	  curr_prob = profile_probability::always ();
	/* Exit block (bb 1) is the block we don't need to process.  */
	if (EXIT_BLOCK_PTR_FOR_FN (cfun) == cfg_bb)
	  continue;

	gcc_assert (curr_prob.initialized_p ());
	FOR_EACH_EDGE (e, ei, cfg_bb->succs)
	  {
	    auto &new_prob = get_block_info (e->dest).probability;
	    /* Normally, the edge probability should be initialized.
	       However, some special testing code which is written in
	       GIMPLE IR style force the edge probability uninitialized,
	       we conservatively set it as never so that it will not
	       affect PRE (Phase 3 && Phase 4).  */
	    if (!e->probability.initialized_p ())
	      new_prob = profile_probability::never ();
	    else if (!new_prob.initialized_p ())
	      new_prob = curr_prob * e->probability;
	    else if (new_prob == profile_probability::always ())
	      continue;
	    else
	      new_prob += curr_prob * e->probability;
	  }
      }
  }

  /* Emit a single register-form vsetvl instruction to establish the
     full IME vtype.  The vtype value is passed as a const_int; the
     @vsetvl_ime<mode> pattern expands it into a "li + vsetvl"
     two-instruction sequence at assembly time, reusing its dest VL
     register as the temporary for the li.  No new pseudos are
     required, which is essential because this runs post-reload.  */

  void insert_ime_vsetvl_insn (enum emit_type emit_type,
			       const vsetvl_info &info)
  {
    rtx_insn *consumer = info.get_insn ()->rtl ();

    /* Lambda elision (see `lambda_already_established_p').  Walk back
       from the consumer; if a matching static DEF is reachable inside
       this basic block with no intervening kill, the existing vtype
       already satisfies the consumer's lambda demand and we skip the
       auto-emission entirely.  Gated by -melide-ime-lambda (default on).  */
    if (info.get_ime_req ().lambda_log2 >= 0
	&& lambda_already_established_p (consumer,
					 info.get_ime_req ().lambda_log2))
      {
	if (dump_file)
	  fprintf (dump_file,
		   "  Elide IME vsetvl insertion before insn %d: lambda=%d "
		   "already established by a preceding DEF in the same BB\n",
		   INSN_UID (consumer), 1 << info.get_ime_req ().lambda_log2);
	return;
      }

    rtx avl = info.get_avl ();
    if (!avl)
      avl = const0_rtx;

    /* Pick a destination/scratch register for the IME vsetvl insn.  The
       @vsetvl_ime pattern reuses its dest register as the scratch for
       the "li" that loads the vtype constant.  If the IME instruction
       we're setting up has its own VL destination (rare -- only for
       vsetvl intrinsics), reuse that; otherwise pick a fallback temp
       based on which form we are about to emit (decided below).
       Static-path `vsetvl_ime' reuses operand 0 as the temp that holds
       the vtype constant (its asm template is "li %0,%2; vsetvl
       %0,%z1,%0") and so requires a real writable GPR -- fall back to
       the prologue temp.  Runtime-path `vsetvl_ime_reg' carries vtype
       in operand 2 and the GPR write of operand 0 is unused, so use x0
       (zero) -- this avoids clobbering a caller-saved register that
       may have been bound by RA to one of the consumer's input
       operands.  We pick `dest_vl' below alongside the pattern.  */
    rtx dest_vl;

    rtx_insn *rinsn = consumer;
    rtx pat;
    if (info.get_ime_req ().lambda_log2 == -1)
      {
	/* Runtime-VLEN path: read the consumer's hidden vtype-source
	   operand (threaded through by the expander; see
	   `apply_ime_vtype_op_p' / the `(use ...)' clause on each IME
	   consumer pattern) and emit a register-form
	   `vsetvl_ime_reg' that re-anchors IME vtype right before the
	   consumer.  This works post-reload because the operand is
	   tracked by RA: by the time we get here it has been
	   substituted with the hard reg the allocator picked for the
	   per-consumer full_vtype pseudo, and that hard reg is alive
	   at the consumer (the `(use)' clause keeps it live across
	   any spill code the pass may insert upstream).  */
	extract_insn_cached (rinsn);
	int idx = get_attr_ime_vtype_op_idx (rinsn);
	if (idx == INVALID_ATTRIBUTE)
	  return;  /* Pattern lacks the operand -- skip.  */
	rtx fvX = recog_data.operand[idx];
	if (!REG_P (fvX))
	  return;  /* Static-VLEN placeholder or spilled -- skip.  */
	dest_vl = info.has_vl () ? info.get_vl ()
				 : gen_rtx_REG (Pmode, 0);
	/* `vsetvl_ime_reg' has both rs1 (avl) and rs2 (vtype) in
	   registers; there is no immediate-AVL form when vtype is in a
	   register.  Materialise const_int AVL into the prologue temp
	   first.  We are post-reload, so we cannot allocate a fresh
	   pseudo; the prologue temp is the conventional choice.  */
	if (avl != const0_rtx && CONST_INT_P (avl))
	  {
	    rtx temp = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
	    emit_insn_before (gen_rtx_SET (temp, avl), rinsn);
	    avl = temp;
	  }
	rtx ime_pack = gen_int_mode (pack_ime_req (info.get_ime_req ()),
				     Pmode);
	pat = gen_vsetvl_ime_reg (Pmode, dest_vl, avl, fvX, ime_pack);
      }
    else
      {
	rtx vtype_const = emit_ime_vtype_build (info);
	if (vtype_const == NULL_RTX)
	  return;  /* sorry() already reported.  */
	dest_vl = info.has_vl ()
		    ? info.get_vl ()
		    : gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
	/* `@vsetvl_ime<mode>' takes AVL via `pmode_reg_or_0_operand'
	   (register or const_int 0); a fused emit can arrive here
	   with the user's compile-time AVL (e.g. from
	   `__riscv_vsetvl_eXmY(N)' for non-zero N) as a const_int.
	   Materialise it into the prologue temp first so the insn
	   matches the pattern's predicate.  We are post-reload, so
	   we cannot allocate a fresh pseudo; the prologue temp is
	   the conventional choice.  */
	if (avl != const0_rtx && CONST_INT_P (avl))
	  {
	    rtx temp = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
	    emit_insn_before (gen_rtx_SET (temp, avl), rinsn);
	    avl = temp;
	  }
	rtx ime_pack = gen_int_mode (pack_ime_req (info.get_ime_req ()),
				     Pmode);
	pat = gen_vsetvl_ime (Pmode, dest_vl, avl, vtype_const, ime_pack);
      }
    rtx_insn *inserted = NULL;

    if (emit_type == EMIT_DIRECT)
      {
	emit_insn (pat);
	inserted = get_last_insn ();
      }
    else if (emit_type == EMIT_BEFORE)
      {
	emit_insn_before (pat, rinsn);
	inserted = PREV_INSN (rinsn);
      }
    else
      {
	emit_insn_after (pat, rinsn);
	inserted = NEXT_INSN (rinsn);
      }

    if (dump_file && inserted)
      {
	fprintf (dump_file, "  Insert IME vsetvl insn %d:\n",
		 INSN_UID (inserted));
	print_rtl_single (dump_file, inserted);
      }
  }

  void insert_vsetvl_insn (enum emit_type emit_type, const vsetvl_info &info)
  {
    if (info.ime_required_p ())
      {
	insert_ime_vsetvl_insn (emit_type, info);
	return;
      }

    rtx_insn *rinsn = info.get_insn ()->rtl ();

    /* Lambda-preserving insertion: a regular vsetvli writes vtype and
       (per WARL) may clobber the lambda field.  If RINSN consumes
       vtype.lambda (an IME tile load/store or mmacc reached via the
       non-IME path) and a DEF reaches at this insertion point, emit
       a vtype-preserving sequence instead of a plain vsetvli:

	 lattice reach = DEFINED(N)
	     `gen_vsetvl_ime' with a const_int vtype = (consumer's
	     static SEW/LMUL/ta/ma) | (lambda field = enc(N)).  One
	     atomic full-vtype write.

	 lattice reach = DEFINED_RUNTIME
	     `gen_riscv_vsetvl_preserve_lambda' which reads the live
	     vtype.lambda at runtime, masks it, ORs in the consumer's
	     static bits, and vsetvls.  Same end effect with the user's
	     runtime-established lambda.  */
    if (emit_type == EMIT_BEFORE
	&& riscv_track_user_lambda
	&& insn_uses_lambda_p (rinsn))
      {
	lambda_state reach = lambda_reach_before (rinsn);
	int xlen = GET_MODE_BITSIZE (Pmode);
	HOST_WIDE_INT static_vtype
	  = compute_static_vtype_without_lambda (info, xlen);
	rtx avl = info.get_avl ();
	if (!avl)
	  avl = const0_rtx;
	rtx dest_vl = info.has_vl ()
			? info.get_vl ()
			: gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);

	/* `@vsetvl_ime<mode>' and `@riscv_vsetvl_preserve_lambda<mode>'
	   take AVL via `pmode_reg_or_0_operand' (register or const_int
	   0).  A non-zero compile-time AVL -- typical when the lambda
	   reach is being threaded into the spot of a user
	   `__riscv_vsetvl_eXmY(N)' for non-zero N -- is rejected by the
	   predicate and produces an unrecognizable insn (PR ICE).
	   Materialise it into the prologue temp first.  We are
	   post-reload, so we cannot allocate a fresh pseudo; the
	   prologue temp is the conventional choice.  */
	if (avl != const0_rtx && CONST_INT_P (avl))
	  {
	    rtx temp = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
	    emit_insn_before (gen_rtx_SET (temp, avl), rinsn);
	    avl = temp;
	  }

	if (reach.kind == lambda_state_kind::DEFINED)
	  {
	    int enc = reach.known_log2 + 1;
	    HOST_WIDE_INT vtype
	      = static_vtype | ((HOST_WIDE_INT) enc << (xlen - 4));
	    /* Lambda-preserving emission for a non-IME consumer: no IME
	       demand to carry through (sew=0 sentinel decodes to "no IME
	       demand" in parse_insn).  */
	    rtx pat = gen_vsetvl_ime (Pmode, dest_vl, avl,
				      gen_int_mode (vtype, Pmode),
				      const0_rtx);
	    emit_insn_before (pat, rinsn);
	    if (dump_file)
	      fprintf (dump_file,
		       "  Insert lambda-preserving vsetvl "
		       "(static lambda=%d) before insn %d\n",
		       1 << reach.known_log2, INSN_UID (rinsn));
	    return;
	  }
	if (reach.kind == lambda_state_kind::DEFINED_RUNTIME)
	  {
	    rtx scratch = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP2_REGNUM);
	    rtx pat = gen_riscv_vsetvl_preserve_lambda (
	      Pmode, dest_vl, avl, gen_int_mode (static_vtype, Pmode),
	      scratch);
	    emit_insn_before (pat, rinsn);
	    if (dump_file)
	      fprintf (dump_file,
		       "  Insert lambda-preserving vsetvl "
		       "(runtime lambda from vtype) before insn %d\n",
		       INSN_UID (rinsn));
	    return;
	  }
      }

    rtx pat = info.get_vsetvl_pat ();

    if (emit_type == EMIT_DIRECT)
      {
	emit_insn (pat);
	if (dump_file)
	  {
	    fprintf (dump_file, "  Insert vsetvl insn %d:\n",
		     INSN_UID (get_last_insn ()));
	    print_rtl_single (dump_file, get_last_insn ());
	  }
      }
    else if (emit_type == EMIT_BEFORE)
      {
	emit_insn_before (pat, rinsn);
	if (dump_file)
	  {
	    fprintf (dump_file, "  Insert vsetvl insn before insn %d:\n",
		     INSN_UID (rinsn));
	    print_rtl_single (dump_file, PREV_INSN (rinsn));
	  }
      }
    else
      {
	emit_insn_after (pat, rinsn);
	if (dump_file)
	  {
	    fprintf (dump_file, "  Insert vsetvl insn after insn %d:\n",
		     INSN_UID (rinsn));
	    print_rtl_single (dump_file, NEXT_INSN (rinsn));
	  }
      }
  }

  void change_vsetvl_insn (const vsetvl_info &info)
  {
    /* IME setups are multi-insn sequences (constant load + register-form
       vsetvl) that cannot be dropped into an existing insn via
       validate_change_or_fail.  They are always inserted fresh, never
       produced by changing an existing vsetvli.  If this trips, the
       caller tried to rewrite a plain vsetvli to an IME setup, which is
       not supported.  */
    gcc_assert (!info.ime_required_p ());

    rtx_insn *rinsn = info.get_insn ()->rtl ();
    rtx new_pat = info.get_vsetvl_pat ();

    if (dump_file)
      {
	fprintf (dump_file, "  Change insn %d from:\n", INSN_UID (rinsn));
	print_rtl_single (dump_file, rinsn);
      }

    validate_change_or_fail (rinsn, &PATTERN (rinsn), new_pat, false);

    if (dump_file)
      {
	fprintf (dump_file, "\n  to:\n");
	print_rtl_single (dump_file, rinsn);
      }
  }

  void remove_vsetvl_insn (rtx_insn *rinsn)
  {
    if (dump_file)
      {
	fprintf (dump_file, "  Eliminate insn %d:\n", INSN_UID (rinsn));
	print_rtl_single (dump_file, rinsn);
      }
    if (in_sequence_p ())
      remove_insn (rinsn);
    else
      delete_insn (rinsn);
  }

  bool successors_probability_equal_p (const basic_block cfg_bb) const
  {
    edge e;
    edge_iterator ei;
    profile_probability prob = profile_probability::uninitialized ();
    FOR_EACH_EDGE (e, ei, cfg_bb->succs)
      {
	if (prob == profile_probability::uninitialized ())
	  prob = m_vector_block_infos[e->dest->index].probability;
	else if (prob == m_vector_block_infos[e->dest->index].probability)
	  continue;
	else
	  /* We pick the highest probability among those incompatible VSETVL
	     infos. When all incompatible VSETVL infos have same probability, we
	     don't pick any of them.  */
	  return false;
      }
    return true;
  }

  bool has_compatible_reaching_vsetvl_p (vsetvl_info info)
  {
    unsigned int index;
    sbitmap_iterator sbi;
    EXECUTE_IF_SET_IN_BITMAP (m_vsetvl_def_in[info.get_bb ()->index ()], 0,
			      index, sbi)
      {
	const auto prev_info = *m_vsetvl_def_exprs[index];
	if (!prev_info.valid_p ())
	  continue;
	if (m_dem.compatible_p (prev_info, info))
	  return true;
      }
    return false;
  }

  bool preds_all_same_avl_and_ratio_p (const vsetvl_info &curr_info)
  {
    gcc_assert (
      !bitmap_empty_p (m_vsetvl_def_in[curr_info.get_bb ()->index ()]));

    unsigned expr_index;
    sbitmap_iterator sbi;
    EXECUTE_IF_SET_IN_BITMAP (m_vsetvl_def_in[curr_info.get_bb ()->index ()], 0,
			      expr_index, sbi)
      {
	const vsetvl_info &prev_info = *m_vsetvl_def_exprs[expr_index];
	if (!prev_info.valid_p ()
	    || !m_dem.avl_available_p (prev_info, curr_info)
	    || prev_info.get_ratio () != curr_info.get_ratio ())
	  return false;
      }

    return true;
  }

public:
  pre_vsetvl ()
    : m_reg_def_loc (nullptr), m_reg_use_loc (nullptr),
      m_vsetvl_def_in (nullptr), m_vsetvl_def_out (nullptr),
      m_vsetvl_avl_reach_in (nullptr), m_vsetvl_avl_reach_out (nullptr),
      m_avloc (nullptr),
      m_avin (nullptr), m_avout (nullptr), m_kill (nullptr), m_antloc (nullptr),
      m_transp (nullptr), m_insert (nullptr), m_del (nullptr), m_edges (nullptr)
  {
    init_rtl_ssa ();
    m_vector_block_infos.safe_grow_cleared (last_basic_block_for_fn (cfun));
    compute_probabilities ();
    m_unknown_info.set_unknown ();
  }

  void finish ()
  {
    finish_rtl_ssa ();

    if (m_reg_def_loc)
      sbitmap_vector_free (m_reg_def_loc);
    if (m_reg_use_loc)
      sbitmap_vector_free (m_reg_use_loc);

    if (m_vsetvl_def_in)
      sbitmap_vector_free (m_vsetvl_def_in);
    if (m_vsetvl_def_out)
      sbitmap_vector_free (m_vsetvl_def_out);

    if (m_vsetvl_avl_reach_in)
      sbitmap_vector_free (m_vsetvl_avl_reach_in);
    if (m_vsetvl_avl_reach_out)
      sbitmap_vector_free (m_vsetvl_avl_reach_out);

    if (m_avloc)
      sbitmap_vector_free (m_avloc);
    if (m_kill)
      sbitmap_vector_free (m_kill);
    if (m_antloc)
      sbitmap_vector_free (m_antloc);
    if (m_transp)
      sbitmap_vector_free (m_transp);
    if (m_insert)
      sbitmap_vector_free (m_insert);
    if (m_del)
      sbitmap_vector_free (m_del);
    if (m_avin)
      sbitmap_vector_free (m_avin);
    if (m_avout)
      sbitmap_vector_free (m_avout);

    if (m_edges)
      free_edge_list (m_edges);
  }

  void compute_vsetvl_def_data ();
  void compute_transparent (const bb_info *);
  void compute_lcm_local_properties ();

  void fuse_local_vsetvl_info ();
  bool earliest_fuse_vsetvl_info (int iter);
  void pre_global_vsetvl_info ();
  void emit_vsetvl ();
  void cleanup ();
  void remove_avl_operand ();
  void remove_unused_dest_operand ();
  void remove_vsetvl_pre_insns ();

  void dump (FILE *file, const char *title) const
  {
    fprintf (file, "\nVSETVL infos after %s\n\n", title);
    for (const bb_info *bb : crtl->ssa->bbs ())
      {
	const auto &block_info = m_vector_block_infos[bb->index ()];
	fprintf (file, "  bb %d:\n", bb->index ());
	fprintf (file, "    probability: ");
	block_info.probability.dump (file);
	fprintf (file, "\n");
	if (!block_info.empty_p ())
	  {
	    fprintf (file, "    Header vsetvl info:");
	    block_info.get_entry_info ().dump (file, "      ");
	    fprintf (file, "    Footer vsetvl info:");
	    block_info.get_exit_info ().dump (file, "      ");
	    for (const auto &info : block_info.local_infos)
	      {
		fprintf (file,
			 "    insn %d vsetvl info:", info.get_insn ()->uid ());
		info.dump (file, "      ");
	      }
	  }
      }
  }
};

void
pre_vsetvl::compute_vsetvl_def_data ()
{
  m_vsetvl_def_exprs.truncate (0);
  add_expr (m_vsetvl_def_exprs, m_unknown_info);
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &footer_info = block_info.get_exit_info ();
      gcc_assert (footer_info.valid_p () || footer_info.unknown_p ());
      add_expr (m_vsetvl_def_exprs, footer_info);
    }

  if (m_vsetvl_def_in)
    sbitmap_vector_free (m_vsetvl_def_in);
  if (m_vsetvl_def_out)
    sbitmap_vector_free (m_vsetvl_def_out);
  if (m_vsetvl_avl_reach_in)
    sbitmap_vector_free (m_vsetvl_avl_reach_in);
  if (m_vsetvl_avl_reach_out)
    sbitmap_vector_free (m_vsetvl_avl_reach_out);

  sbitmap *def_loc = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
					   m_vsetvl_def_exprs.length ());
  sbitmap *m_kill = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
					  m_vsetvl_def_exprs.length ());

  m_vsetvl_def_in = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
					  m_vsetvl_def_exprs.length ());
  m_vsetvl_def_out = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
					   m_vsetvl_def_exprs.length ());

  m_vsetvl_avl_reach_in
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), GP_REG_LAST + 1);
  m_vsetvl_avl_reach_out
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), GP_REG_LAST + 1);

  bitmap_vector_clear (def_loc, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_kill, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_vsetvl_def_out, last_basic_block_for_fn (cfun));

  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	{
	  for (unsigned i = 0; i < m_vsetvl_def_exprs.length (); i += 1)
	    {
	      auto *info = m_vsetvl_def_exprs[i];
	      if (info->has_nonvlmax_reg_avl ()
		  && bitmap_bit_p (m_reg_def_loc[bb->index ()],
				   REGNO (info->get_avl ())))
		{
		  bitmap_set_bit (m_kill[bb->index ()], i);
		  bitmap_set_bit (def_loc[bb->index ()],
				  get_expr_index (m_vsetvl_def_exprs,
						  m_unknown_info));
		}
	    }
	  continue;
	}

      vsetvl_info &footer_info = block_info.get_exit_info ();
      bitmap_ones (m_kill[bb->index ()]);
      bitmap_set_bit (def_loc[bb->index ()],
		      get_expr_index (m_vsetvl_def_exprs, footer_info));

      /* WARL-preservation refinement on the reaching-definition kill.
	 When the footer's basic geometry (SEW, VLMUL, ta, ma) matches an
	 expression's basic geometry, the footer's vtype write preserves
	 the IME-specific bits of that expression (RISC-V vector spec
	 §319-320, §322-336, §369-371: lambda preserved when (VLEN, SEW)
	 still supports it; altfmt_A/B/bs always preserved across
	 vsetvli/vsetivli/vsetvl).  Don't kill compatible IME expressions
	 across such a footer.  */
      if (footer_info.valid_p ())
	for (unsigned i = 0; i < m_vsetvl_def_exprs.length (); i++)
	  {
	    const auto &info = *m_vsetvl_def_exprs[i];
	    if (!info.valid_p ())
	      continue;
	    if (footer_info.get_sew () != info.get_sew ()
		|| footer_info.get_vlmul () != info.get_vlmul ()
		|| footer_info.get_ta () != info.get_ta ()
		|| footer_info.get_ma () != info.get_ma ())
	      continue;
	    /* When both sides have an IME demand, the IME tuples must
	       also match -- otherwise the footer rewrites lambda/altfmt
	       to a different value and the prior IME state is destroyed.  */
	    if (footer_info.ime_required_p () && info.ime_required_p ()
		&& !(footer_info.get_ime_req () == info.get_ime_req ()))
	      continue;
	    bitmap_clear_bit (m_kill[bb->index ()], i);
	  }
    }

  /* Set the def_out of the ENTRY basic block to m_unknown_info expr.  */
  basic_block entry = ENTRY_BLOCK_PTR_FOR_FN (cfun);
  bitmap_set_bit (m_vsetvl_def_out[entry->index],
		  get_expr_index (m_vsetvl_def_exprs, m_unknown_info));

  compute_reaching_definition (def_loc, m_kill, m_vsetvl_def_in,
			       m_vsetvl_def_out);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file,
	       "\n  Compute vsetvl info reaching definition data:\n\n");
      fprintf (dump_file, "    Expression List (%d):\n",
	       m_vsetvl_def_exprs.length ());
      for (unsigned i = 0; i < m_vsetvl_def_exprs.length (); i++)
	{
	  const auto &info = *m_vsetvl_def_exprs[i];
	  fprintf (dump_file, "      Expr[%u]: ", i);
	  info.dump (dump_file, "        ");
	}
      fprintf (dump_file, "\n    bitmap data:\n");
      for (const bb_info *bb : crtl->ssa->bbs ())
	{
	  unsigned int i = bb->index ();
	  fprintf (dump_file, "      BB %u:\n", i);
	  fprintf (dump_file, "        def_loc: ");
	  dump_bitmap_file (dump_file, def_loc[i]);
	  fprintf (dump_file, "        kill: ");
	  dump_bitmap_file (dump_file, m_kill[i]);
	  fprintf (dump_file, "        vsetvl_def_in: ");
	  dump_bitmap_file (dump_file, m_vsetvl_def_in[i]);
	  fprintf (dump_file, "        vsetvl_def_out: ");
	  dump_bitmap_file (dump_file, m_vsetvl_def_out[i]);
	}
    }

  sbitmap_vector_free (def_loc);
  sbitmap_vector_free (m_kill);

  /* Now compute the reaching definitions for AVL operands.
     We can reuse def_loc but index it by regnos now.  */
  def_loc = sbitmap_vector_alloc (last_basic_block_for_fn (cfun),
				  GP_REG_LAST + 1);

  bitmap_vector_clear (def_loc, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_vsetvl_avl_reach_out, last_basic_block_for_fn (cfun));

  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &info = block_info.get_exit_info ();
      if (info.has_vl ())
	bitmap_set_bit (def_loc[bb->index ()], REGNO (info.get_vl ()));
    }

  compute_reaching_definition (def_loc, m_reg_def_loc, m_vsetvl_avl_reach_in,
			       m_vsetvl_avl_reach_out);
}

/* Subroutine of compute_lcm_local_properties which Compute local transparent
   BB. Note that the compile time is very sensitive to compute_transparent and
   compute_lcm_local_properties, any change of these 2 functions should be
   aware of the compile time changing of the program which has a large number of
   blocks, e.g SPEC 2017 wrf.

   Current compile time profile of SPEC 2017 wrf:

     1. scheduling - 27%
     2. machine dep reorg (VSETVL PASS) - 18%

   VSETVL pass should not spend more time than scheduling in compilation.  */
void
pre_vsetvl::compute_transparent (const bb_info *bb)
{
  int num_exprs = m_exprs.length ();
  unsigned bb_index = bb->index ();
  for (int i = 0; i < num_exprs; i++)
    {
      auto *info = m_exprs[i];
      if (info->has_nonvlmax_reg_avl ()
	  && bitmap_bit_p (m_reg_def_loc[bb_index], REGNO (info->get_avl ())))
	bitmap_clear_bit (m_transp[bb_index], i);
      else if (info->has_vl ())
	{
	  /* If the VL reg is redefined, we cannot move a vsetvl past it.  */
	  if (bitmap_bit_p (m_reg_def_loc[bb_index],
			    REGNO (info->get_vl ())))
	    bitmap_clear_bit (m_transp[bb_index], i);
	  /* Same if there is a VL reg use that didn't come from a vsetvl.  */
	  else if (bitmap_bit_p (m_reg_use_loc[bb_index],
				 REGNO (info->get_vl ()))
		   && !bitmap_bit_p (m_vsetvl_avl_reach_in[bb_index],
				     REGNO (info->get_vl ())))
	    bitmap_clear_bit (m_transp[bb_index], i);
	}
    }
}

/* Compute the local properties of each recorded expression.

   Local properties are those that are defined by the block, irrespective of
   other blocks.

   An expression is transparent in a block if its operands are not modified
   in the block.

   An expression is computed (locally available) in a block if it is computed
   at least once and expression would contain the same value if the
   computation was moved to the end of the block.

   An expression is locally anticipatable in a block if it is computed at
   least once and expression would contain the same value if the computation
   was moved to the beginning of the block.  */
void
pre_vsetvl::compute_lcm_local_properties ()
{
  m_exprs.truncate (0);
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &header_info = block_info.get_entry_info ();
      vsetvl_info &footer_info = block_info.get_exit_info ();
      gcc_assert (footer_info.valid_p () || footer_info.unknown_p ());
      if (header_info.valid_p ())
	add_expr (m_exprs, header_info);
      if (footer_info.valid_p ())
	add_expr (m_exprs, footer_info);
    }

  int num_exprs = m_exprs.length ();
  if (m_avloc)
    sbitmap_vector_free (m_avloc);
  if (m_kill)
    sbitmap_vector_free (m_kill);
  if (m_antloc)
    sbitmap_vector_free (m_antloc);
  if (m_transp)
    sbitmap_vector_free (m_transp);
  if (m_avin)
    sbitmap_vector_free (m_avin);
  if (m_avout)
    sbitmap_vector_free (m_avout);

  m_avloc = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_kill = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_antloc = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_transp = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_avin = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  m_avout = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);

  bitmap_vector_clear (m_avloc, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_kill, last_basic_block_for_fn (cfun));
  bitmap_vector_clear (m_antloc, last_basic_block_for_fn (cfun));
  bitmap_vector_ones (m_transp, last_basic_block_for_fn (cfun));

  /* -  If T is locally available at the end of a block, then T' must be
	available at the end of the same block. Since some optimization has
	occurred earlier, T' might not be locally available, however, it must
	have been previously computed on all paths. As a formula, T at AVLOC(B)
	implies that T' at AVOUT(B).
	An "available occurrence" is one that is the last occurrence in the
	basic block and the operands are not modified by following statements in
	the basic block [including this insn].

     -  If T is locally anticipated at the beginning of a block, then either
	T', is locally anticipated or it is already available from previous
	blocks. As a formula, this means that T at ANTLOC(B) implies that T' at
	ANTLOC(B) at AVIN(B).
	An "anticipatable occurrence" is one that is the first occurrence in the
	basic block, the operands are not modified in the basic block prior
	to the occurrence and the output is not used between the start of
	the block and the occurrence.  */
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      unsigned bb_index = bb->index ();
      vsetvl_block_info &block_info = get_block_info (bb);

      /* Compute m_transp */
      if (block_info.empty_p ())
	compute_transparent (bb);
      else
	{
	  bitmap_clear (m_transp[bb_index]);
	  vsetvl_info &header_info = block_info.get_entry_info ();
	  vsetvl_info &footer_info = block_info.get_exit_info ();

	  if (header_info.valid_p () && anticipated_exp_p (header_info))
	    bitmap_set_bit (m_antloc[bb_index],
			    get_expr_index (m_exprs, header_info));

	  if (footer_info.valid_p ())
	    for (int i = 0; i < num_exprs; i += 1)
	      {
		const vsetvl_info &info = *m_exprs[i];
		if (!info.valid_p ())
		  continue;
		if (available_exp_p (footer_info, info))
		  bitmap_set_bit (m_avloc[bb_index], i);
	      }
	}

      if (invalid_opt_bb_p (bb->cfg_bb ()))
	{
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file, "\n --- skipping bb %u due to weird edge",
		     bb->index ());

	  bitmap_clear (m_antloc[bb_index]);
	  bitmap_clear (m_transp[bb_index]);
	}

      /* Compute ae_kill for each basic block using:

	 ~(TRANSP | COMP)
      */
      bitmap_ior (m_kill[bb_index], m_transp[bb_index], m_avloc[bb_index]);
      bitmap_not (m_kill[bb_index], m_kill[bb_index]);
    }
}

void
pre_vsetvl::fuse_local_vsetvl_info ()
{
  m_reg_def_loc
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), GP_REG_LAST + 1);
  bitmap_vector_clear (m_reg_def_loc, last_basic_block_for_fn (cfun));
  bitmap_ones (m_reg_def_loc[ENTRY_BLOCK_PTR_FOR_FN (cfun)->index]);

  m_reg_use_loc
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), GP_REG_LAST + 1);
  bitmap_vector_clear (m_reg_use_loc, last_basic_block_for_fn (cfun));

  /* No need to track all GPRs, just use those that are VL destinations.
     Store them in a bitmap for filtering the uses later on.  */
  auto_bitmap vsetvl_dest_regs;
  for (bb_info *bb : crtl->ssa->bbs ())
    for (insn_info *insn : bb->real_nondebug_insns ())
      {
	vsetvl_info info = vsetvl_info (insn);
	if (info.valid_p () && info.has_vl ())
	  bitmap_set_bit (vsetvl_dest_regs, REGNO (info.get_vl ()));
      }

  for (bb_info *bb : crtl->ssa->bbs ())
    {
      auto &block_info = get_block_info (bb);
      block_info.bb = bb;
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file, "  Try fuse basic block %d\n", bb->index ());
	}
      auto_vec<vsetvl_info> infos;
      for (insn_info *insn : bb->real_nondebug_insns ())
	{
	  vsetvl_info curr_info = vsetvl_info (insn);
	  if (curr_info.valid_p () || curr_info.unknown_p ())
	    infos.safe_push (curr_info);

	  if (insn->is_real ())
	    {
	      /* Collect GPRs modified by the current bb.  */
	      for (def_info *def : insn->defs ())
		if (def->is_reg () && GP_REG_P (def->regno ()))
		  bitmap_set_bit (m_reg_def_loc[bb->index ()], def->regno ());
	      /* Collect non-vsetvl uses of GPRs.  */
	      if (!curr_info.valid_p ())
		{
		  for (use_info *use : insn->uses ())
		    if (use->is_reg () && GP_REG_P (use->regno ())
			&& bitmap_bit_p (vsetvl_dest_regs, use->regno ()))
		      bitmap_set_bit (m_reg_use_loc[bb->index ()],
				      use->regno ());
		}
	    }
	}

      vsetvl_info prev_info = vsetvl_info ();
      prev_info.set_empty ();
      for (auto &curr_info : infos)
	{
	  if (prev_info.empty_p ())
	    prev_info = curr_info;
	  else if ((curr_info.unknown_p () && prev_info.valid_p ())
		   || (curr_info.valid_p () && prev_info.unknown_p ()))
	    {
	      block_info.local_infos.safe_push (prev_info);
	      prev_info = curr_info;
	    }
	  else if (curr_info.valid_p () && prev_info.valid_p ())
	    {
	      if (m_dem.available_p (prev_info, curr_info))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file,
			       "    Ignore curr info since prev info "
			       "available with it:\n");
		      fprintf (dump_file, "      prev_info: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "      curr_info: ");
		      curr_info.dump (dump_file, "        ");
		      fprintf (dump_file, "\n");
		    }
		  /* Even though prev_info is available with curr_info,
		     we need to update the MAX_SEW of prev_info since
		     we don't check MAX_SEW in available_p check.

		     prev_info:
		     Demand fields: demand_ratio_and_ge_sew demand_avl
		     SEW=16, VLMUL=mf4, RATIO=64, MAX_SEW=64

		     curr_info:
		     Demand fields: demand_ge_sew demand_non_zero_avl
		     SEW=16, VLMUL=m1, RATIO=16, MAX_SEW=32

		     In the example above, prev_info is available with
		     curr_info, we need to update prev_info MAX_SEW from
		     64 into 32.  */
		  prev_info.set_max_sew (
		    MIN (prev_info.get_max_sew (), curr_info.get_max_sew ()));

		  /* If we fuse and the current, to be deleted vsetvl has uses
		     of its VL as AVL operand in other vsetvls those will be
		     orphaned.  Therefore only delete if that's not the case.
		     */
		  rtx cur_dest = curr_info.has_vl ()
		    ? curr_info.get_vl ()
		    : NULL_RTX;

		  if (vsetvl_insn_p (curr_info.get_insn ()->rtl ())
		      && !reg_single_use_in_avl (curr_info.get_insn (),
						 cur_dest))
		    m_delete_list.safe_push (curr_info);

		  if (curr_info.get_read_vl_insn ())
		    prev_info.set_read_vl_insn (curr_info.get_read_vl_insn ());
		}
	      else if (m_dem.compatible_p (prev_info, curr_info))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "    Fuse curr info since prev info "
					  "compatible with it:\n");
		      fprintf (dump_file, "      prev_info: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "      curr_info: ");
		      curr_info.dump (dump_file, "        ");
		    }
		  m_dem.merge (prev_info, curr_info);
		  if (!curr_info.vl_used_by_non_rvv_insn_p ()
		      && vsetvl_insn_p (curr_info.get_insn ()->rtl ()))
		    m_delete_list.safe_push (curr_info);
		  if (curr_info.get_read_vl_insn ())
		    prev_info.set_read_vl_insn (curr_info.get_read_vl_insn ());
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "      prev_info after fused: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "\n");
		    }
		}
	      else
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file,
			       "    Cannot fuse incompatible infos:\n");
		      fprintf (dump_file, "      prev_info: ");
		      prev_info.dump (dump_file, "       ");
		      fprintf (dump_file, "      curr_info: ");
		      curr_info.dump (dump_file, "       ");
		    }
		  block_info.local_infos.safe_push (prev_info);
		  prev_info = curr_info;
		}
	    }
	}

      if (prev_info.valid_p () || prev_info.unknown_p ())
	block_info.local_infos.safe_push (prev_info);
    }
}


bool
pre_vsetvl::earliest_fuse_vsetvl_info (int iter)
{
  compute_vsetvl_def_data ();
  compute_lcm_local_properties ();

  unsigned num_exprs = m_exprs.length ();
  struct edge_list *m_edges = create_edge_list ();
  unsigned num_edges = NUM_EDGES (m_edges);
  sbitmap *antin
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);
  sbitmap *antout
    = sbitmap_vector_alloc (last_basic_block_for_fn (cfun), num_exprs);

  sbitmap *earliest = sbitmap_vector_alloc (num_edges, num_exprs);

  compute_available (m_avloc, m_kill, m_avout, m_avin);
  compute_antinout_edge (m_antloc, m_transp, antin, antout);
  compute_earliest (m_edges, num_exprs, antin, antout, m_avout, m_kill,
		    earliest);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\n  Compute LCM earliest insert data (lift %d):\n\n",
	       iter);
      fprintf (dump_file, "    Expression List (%u):\n", num_exprs);
      for (unsigned i = 0; i < num_exprs; i++)
	{
	  const auto &info = *m_exprs[i];
	  fprintf (dump_file, "      Expr[%u]: ", i);
	  info.dump (dump_file, "        ");
	}
      fprintf (dump_file, "\n    bitmap data:\n");
      for (const bb_info *bb : crtl->ssa->bbs ())
	{
	  unsigned int i = bb->index ();
	  fprintf (dump_file, "      BB %u:\n", i);
	  fprintf (dump_file, "        avloc: ");
	  dump_bitmap_file (dump_file, m_avloc[i]);
	  fprintf (dump_file, "        kill: ");
	  dump_bitmap_file (dump_file, m_kill[i]);
	  fprintf (dump_file, "        antloc: ");
	  dump_bitmap_file (dump_file, m_antloc[i]);
	  fprintf (dump_file, "        transp: ");
	  dump_bitmap_file (dump_file, m_transp[i]);

	  fprintf (dump_file, "        avin: ");
	  dump_bitmap_file (dump_file, m_avin[i]);
	  fprintf (dump_file, "        avout: ");
	  dump_bitmap_file (dump_file, m_avout[i]);
	  fprintf (dump_file, "        antin: ");
	  dump_bitmap_file (dump_file, antin[i]);
	  fprintf (dump_file, "        antout: ");
	  dump_bitmap_file (dump_file, antout[i]);
	}
      fprintf (dump_file, "\n");
      fprintf (dump_file, "      earliest:\n");
      for (unsigned ed = 0; ed < num_edges; ed++)
	{
	  edge eg = INDEX_EDGE (m_edges, ed);

	  if (bitmap_empty_p (earliest[ed]))
	    continue;
	  fprintf (dump_file, "        Edge(bb %u -> bb %u): ", eg->src->index,
		   eg->dest->index);
	  dump_bitmap_file (dump_file, earliest[ed]);
	}
      fprintf (dump_file, "\n");
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "    Fused global info result (lift %d):\n", iter);
    }

  bool changed = false;
  for (unsigned ed = 0; ed < num_edges; ed++)
    {
      sbitmap e = earliest[ed];
      if (bitmap_empty_p (e))
	continue;

      unsigned int expr_index;
      sbitmap_iterator sbi;
      EXECUTE_IF_SET_IN_BITMAP (e, 0, expr_index, sbi)
	{
	  vsetvl_info &curr_info = *m_exprs[expr_index];
	  edge eg = INDEX_EDGE (m_edges, ed);
	  vsetvl_block_info &src_block_info = get_block_info (eg->src);
	  vsetvl_block_info &dest_block_info = get_block_info (eg->dest);

	  if (!curr_info.valid_p ()
	      || eg->probability == profile_probability::never ()
	      || src_block_info.probability
		   == profile_probability::uninitialized ()
	      /* When multiple set bits in earliest edge, such edge may
		 have infinite loop in preds or succs or multiple conflict
		 vsetvl expression which make such edge is unrelated.  We
		 don't perform fusion for such situation.  */
	      || bitmap_count_bits (e) != 1)
	    continue;

	  if (src_block_info.empty_p ())
	    {
	      vsetvl_info new_curr_info = curr_info;
	      new_curr_info.set_bb (crtl->ssa->bb (eg->dest));
	      bool has_compatible_p
		= has_compatible_reaching_vsetvl_p (new_curr_info);
	      if (!has_compatible_p)
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file,
			       "      Forbidden lift up vsetvl info into bb %u "
			       "since there is no vsetvl info that reaching in "
			       "is compatible with it:",
			       eg->src->index);
		      curr_info.dump (dump_file, "        ");
		    }
		  continue;
		}

	      /* We cannot lift a vsetvl into the source block if the block is
		 not transparent WRT to it.
		 This is too restrictive for blocks where a register's use only
		 feeds into vsetvls and no regular insns.  One example is the
		 test rvv/vsetvl/avl_single-68.c which is currently XFAILed for
		 that reason.
		 In order to support this case we'd need to check the vsetvl's
		 AVL operand's uses in the source block and make sure they are
		 only used in other vsetvls.  */
	      if (!bitmap_bit_p (m_transp[eg->src->index], expr_index))
		continue;

	      /* Transparency tells us if we can move upwards without looking
		 down.  It is still possible to clobber non-vsetvl uses
		 that happen to share the vsetvl destination register of the
		 vsetvl we are about to hoist.
		 As we have computed the vsetvl VL dest -> vsetvl AVL reach
		 before, we can check if our VL register is live-in for each
		 successor and not reached by a vsetvl.  If so, we cannot
		 hoist, as that would clobber the use.  */
	      if (curr_info.has_vl ())
		{
		  edge succ;
		  edge_iterator it;
		  bool clobber = false;
		  FOR_EACH_EDGE (succ, it, eg->src->succs)
		    {
		      if (succ->dest == eg->dest)
			continue;
		      if (bitmap_bit_p (df_get_live_in (succ->dest),
					REGNO (curr_info.get_vl ()))
			  && !bitmap_bit_p
			  (m_vsetvl_avl_reach_in[succ->dest->index],
			   REGNO (curr_info.get_vl ())))
			{
			  clobber = true;
			  break;
			}
		    }
		  if (clobber)
		    continue;
		}


	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file,
			   "      Hoisting vsetvl info from bb %u to "
			   "bb %u: ", eg->dest->index, eg->src->index);
		  curr_info.dump (dump_file, "        ");
		}
	      src_block_info.set_info (curr_info);
	      src_block_info.probability = dest_block_info.probability;
	      changed = true;
	    }
	  else if (src_block_info.has_info ())
	    {
	      vsetvl_info &prev_info = src_block_info.get_exit_info ();
	      gcc_assert (prev_info.valid_p ());

	      if (m_dem.compatible_p (prev_info, curr_info))
		{
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "    Fuse curr info since prev info "
					  "compatible with it:\n");
		      fprintf (dump_file, "      prev_info: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "      curr_info: ");
		      curr_info.dump (dump_file, "        ");
		    }
		  m_dem.merge (prev_info, curr_info);
		  if (dump_file && (dump_flags & TDF_DETAILS))
		    {
		      fprintf (dump_file, "      prev_info after fused: ");
		      prev_info.dump (dump_file, "        ");
		      fprintf (dump_file, "\n");
		    }
		  changed = true;
		  if (src_block_info.has_info ())
		    src_block_info.probability += dest_block_info.probability;
		}
	      else
		{
		  /* Cancel lift up if probabilities are equal.  */
		  if (successors_probability_equal_p (eg->src)
		      || (dest_block_info.probability
			    > src_block_info.probability
			  && !has_compatible_reaching_vsetvl_p (curr_info)))
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  fprintf (dump_file,
				   "      Reset bb %u:",
				   eg->src->index);
			  prev_info.dump (dump_file, "        ");
			  fprintf (dump_file, "	due to (same probability or no "
					      "compatible reaching):");
			  curr_info.dump (dump_file, "        ");
			}
		      src_block_info.set_empty_info ();
		      src_block_info.probability
			= profile_probability::uninitialized ();
		      /* See PR113696, we should reset immediate dominator to
			 empty since we may uplift ineffective vsetvl which
			 locate at low probability block.  */
		      basic_block dom
			= get_immediate_dominator (CDI_DOMINATORS, eg->src);
		      auto &dom_block_info = get_block_info (dom);
		      if (dom_block_info.has_info ()
			  && !m_dem.compatible_p (
			    dom_block_info.get_exit_info (), curr_info))
			{
			  dom_block_info.set_empty_info ();
			  dom_block_info.probability
			    = profile_probability::uninitialized ();
			  if (dump_file && (dump_flags & TDF_DETAILS))
			    {
			      fprintf (dump_file,
				       "      Reset dominator bb %u:",
				       dom->index);
			      prev_info.dump (dump_file, "        ");
			      fprintf (dump_file,
				       "	due to (same probability or no "
				       "compatible reaching):");
			      curr_info.dump (dump_file, "        ");
			    }
			}
		      changed = true;
		    }
		  /* Choose the one with higher probability. */
		  else if (dest_block_info.probability
			   > src_block_info.probability)
		    {
		      if (dump_file && (dump_flags & TDF_DETAILS))
			{
			  fprintf (dump_file,
				   "      Change bb %u from:",
				   eg->src->index);
			  prev_info.dump (dump_file, "        ");
			  fprintf (dump_file,
				   "        to (higher probability):");
			  curr_info.dump (dump_file, "        ");
			}
		      src_block_info.set_info (curr_info);
		      src_block_info.probability = dest_block_info.probability;
		      changed = true;
		    }
		}
	    }
	  else
	    {
	      vsetvl_info &prev_info = src_block_info.get_exit_info ();
	      if (!prev_info.valid_p ()
		  || m_dem.available_p (prev_info, curr_info)
		  || !m_dem.compatible_p (prev_info, curr_info))
		continue;

	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "    Fuse curr info since prev info "
				      "compatible with it:\n");
		  fprintf (dump_file, "      prev_info: ");
		  prev_info.dump (dump_file, "        ");
		  fprintf (dump_file, "      curr_info: ");
		  curr_info.dump (dump_file, "        ");
		}
	      m_dem.merge (prev_info, curr_info);
	      if (dump_file && (dump_flags & TDF_DETAILS))
		{
		  fprintf (dump_file, "      prev_info after fused: ");
		  prev_info.dump (dump_file, "        ");
		  fprintf (dump_file, "\n");
		}
	      changed = true;
	    }
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\n");
    }

  sbitmap_vector_free (antin);
  sbitmap_vector_free (antout);
  sbitmap_vector_free (earliest);
  free_edge_list (m_edges);

  return changed;
}

void
pre_vsetvl::pre_global_vsetvl_info ()
{
  compute_vsetvl_def_data ();
  compute_lcm_local_properties ();

  unsigned num_exprs = m_exprs.length ();
  m_edges = pre_edge_lcm_avs (num_exprs, m_transp, m_avloc, m_antloc, m_kill,
			      m_avin, m_avout, &m_insert, &m_del);
  unsigned num_edges = NUM_EDGES (m_edges);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\n  Compute LCM insert and delete data:\n\n");
      fprintf (dump_file, "    Expression List (%u):\n", num_exprs);
      for (unsigned i = 0; i < num_exprs; i++)
	{
	  const auto &info = *m_exprs[i];
	  fprintf (dump_file, "      Expr[%u]: ", i);
	  info.dump (dump_file, "        ");
	}
      fprintf (dump_file, "\n    bitmap data:\n");
      for (const bb_info *bb : crtl->ssa->bbs ())
	{
	  unsigned i = bb->index ();
	  fprintf (dump_file, "      BB %u:\n", i);
	  fprintf (dump_file, "        avloc: ");
	  dump_bitmap_file (dump_file, m_avloc[i]);
	  fprintf (dump_file, "        kill: ");
	  dump_bitmap_file (dump_file, m_kill[i]);
	  fprintf (dump_file, "        antloc: ");
	  dump_bitmap_file (dump_file, m_antloc[i]);
	  fprintf (dump_file, "        transp: ");
	  dump_bitmap_file (dump_file, m_transp[i]);

	  fprintf (dump_file, "        avin: ");
	  dump_bitmap_file (dump_file, m_avin[i]);
	  fprintf (dump_file, "        avout: ");
	  dump_bitmap_file (dump_file, m_avout[i]);
	  fprintf (dump_file, "        del: ");
	  dump_bitmap_file (dump_file, m_del[i]);
	}
      fprintf (dump_file, "\n");
      fprintf (dump_file, "      insert:\n");
      for (unsigned ed = 0; ed < num_edges; ed++)
	{
	  edge eg = INDEX_EDGE (m_edges, ed);

	  if (bitmap_empty_p (m_insert[ed]))
	    continue;
	  fprintf (dump_file, "        Edge(bb %u -> bb %u): ", eg->src->index,
		   eg->dest->index);
	  dump_bitmap_file (dump_file, m_insert[ed]);
	}
    }

  /* Remove vsetvl infos as LCM suggest */
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      sbitmap d = m_del[bb->index ()];
      if (bitmap_count_bits (d) == 0)
	continue;
      gcc_assert (bitmap_count_bits (d) == 1);
      unsigned expr_index = bitmap_first_set_bit (d);
      vsetvl_info &info = *m_exprs[expr_index];
      gcc_assert (info.valid_p ());
      gcc_assert (info.get_bb () == bb);
      const vsetvl_block_info &block_info = get_block_info (info.get_bb ());
      gcc_assert (block_info.get_entry_info () == info);
      info.set_delete ();
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
		   "\nLCM deleting vsetvl of block %d, it has predecessors: \n",
		   bb->index ());
	  hash_set<basic_block> all_preds
	    = get_all_predecessors (bb->cfg_bb ());
	  int i = 0;
	  for (const auto pred : all_preds)
	    {
	      fprintf (dump_file, "%d ", pred->index);
	      i++;
	      if (i % 32 == 0)
		fprintf (dump_file, "\n");
	    }
	  fprintf (dump_file, "\n");
	}
    }

  /* Remove vsetvl infos if all predecessors are available to the block.  */
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &curr_info = block_info.get_entry_info ();
      if (!curr_info.valid_p ())
	continue;

      unsigned int expr_index;
      sbitmap_iterator sbi;
      gcc_assert (
	!bitmap_empty_p (m_vsetvl_def_in[curr_info.get_bb ()->index ()]));
      bool full_available = true;
      EXECUTE_IF_SET_IN_BITMAP (m_vsetvl_def_in[bb->index ()], 0, expr_index,
				sbi)
	{
	  vsetvl_info &prev_info = *m_vsetvl_def_exprs[expr_index];
	  if (!prev_info.valid_p ()
	      || !m_dem.available_p (prev_info, curr_info))
	    {
	      full_available = false;
	      break;
	    }
	}
      if (full_available)
	curr_info.set_delete ();
    }

  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      vsetvl_block_info &block_info = get_block_info (bb);
      if (block_info.empty_p ())
	continue;
      vsetvl_info &curr_info = block_info.get_entry_info ();
      if (curr_info.delete_p ())
	{
	  if (block_info.local_infos.is_empty ())
	    continue;
	  curr_info = block_info.local_infos[0];
	}
      if (curr_info.valid_p () && !curr_info.vl_used_by_non_rvv_insn_p ()
	  && !is_preserve_lambda_p (curr_info.get_insn ()->rtl ())
	  && preds_all_same_avl_and_ratio_p (curr_info))
	curr_info.set_change_vtype_only ();

      vsetvl_info prev_info = vsetvl_info ();
      prev_info.set_empty ();
      for (auto &curr_info : block_info.local_infos)
	{
	  if (prev_info.valid_p () && curr_info.valid_p ()
	      && !is_preserve_lambda_p (curr_info.get_insn ()->rtl ())
	      && m_dem.avl_available_p (prev_info, curr_info)
	      && prev_info.get_ratio () == curr_info.get_ratio ())
	    curr_info.set_change_vtype_only ();
	  prev_info = curr_info;
	}
    }
}

void
pre_vsetvl::emit_vsetvl ()
{
  bool need_commit = false;

  /* Fake edge is created by connect infinite loops to exit function.
     We should commit vsetvl edge after fake edges removes, otherwise,
     it will cause ICE.  */
  remove_fake_exit_edges ();
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      /* Track whether any prior local_info in this BB has actually
	 emitted (or kept) a vsetvl.  The WARL-aware skip below relies
	 on `m_vsetvl_def_in[bb]', which only reflects state at BB
	 entry; once a prior local_info has emitted a vsetvl, the
	 reaching state at the consumer's position is determined by
	 that emission, not by def_in.  delete_p local_infos do NOT
	 flip this flag (they emit nothing).  */
      bool prior_emission_in_bb = false;
      for (const auto &curr_info : get_block_info (bb).local_infos)
	{
	  insn_info *insn = curr_info.get_insn ();
	  if (curr_info.delete_p ())
	    {
	      if (vsetvl_insn_p (insn->rtl ()))
		remove_vsetvl_insn (curr_info.get_insn ()->rtl ());
	      continue;
	    }
	  else if (curr_info.valid_p ())
	    {
	      if (vsetvl_insn_p (insn->rtl ()))
		{
		  const vsetvl_info temp = vsetvl_info (insn);
		  if (!(curr_info == temp))
		    {
		      if (dump_file)
			{
			  fprintf (dump_file, "\n  Change vsetvl info from: ");
			  temp.dump (dump_file, "    ");
			  fprintf (dump_file, "  to: ");
			  curr_info.dump (dump_file, "    ");
			}
		      change_vsetvl_insn (curr_info);
		    }
		  prior_emission_in_bb = true;
		}
	      else if (is_preserve_lambda_p (insn->rtl ()))
		{
		  /* preserve_lambda's own local_info: skip.  It is its
		     own vtype source, and lifting a plain vsetvli above
		     it would clobber the live vtype.lambda before its
		     csrr-and-merge reads it.  */
		}
	      else
		{
		  /* WARL-aware skip for IME consumers: if the reaching
		     def set at this BB contains an expression that
		     satisfies curr_info's IME demand AND every other
		     reaching def is a basic vsetvl at compatible
		     geometry (so its write preserves the IME bits via
		     WARL -- RISC-V vector spec §319-320, §322-336,
		     §369-371), no fresh IME vsetvl needs to be emitted.  */
		  bool reach_satisfies = false;
		  /* The def_in bitmap only reflects state at BB ENTRY.
		     If a vsetvl-emitting local_info precedes curr_info
		     in the same BB, IT determines the state at
		     curr_info's position, not def_in.  Restrict the skip
		     to the case where curr_info IS the BB entry info --
		     then def_in is the actual reaching state.  A more
		     general position-aware analysis would also catch
		     skip opportunities where every intervening local_info
		     is itself a propagating WARL-compatible vsetvl;
		     keeping it conservative for now until validated.  */
		  if (curr_info.ime_required_p ()
		      && !prior_emission_in_bb
		      && m_vsetvl_def_in
		      && !bitmap_empty_p (m_vsetvl_def_in[bb->index ()]))
		    {
		      bool any_ime_satisfies = false;
		      bool all_compatible = true;
		      unsigned int expr_index;
		      sbitmap_iterator sbi;
		      EXECUTE_IF_SET_IN_BITMAP (m_vsetvl_def_in[bb->index ()],
						0, expr_index, sbi)
			{
			  vsetvl_info &prev
			    = *m_vsetvl_def_exprs[expr_index];
			  if (!prev.valid_p ()
			      || prev.get_sew () != curr_info.get_sew ()
			      || prev.get_vlmul () != curr_info.get_vlmul ()
			      || prev.get_ta () != curr_info.get_ta ()
			      || prev.get_ma () != curr_info.get_ma ())
			    {
			      all_compatible = false;
			      break;
			    }
			  /* AVL must also match: skipping the consumer's
			     vsetvl means VL stays at whatever the last
			     reaching def wrote.  If prev's AVL differs
			     from curr_info's, VL after the skip is wrong
			     for the consumer.  avl_available_p handles
			     imm-vs-imm, reg-vs-reg and VLMAX rules.  */
			  if (!m_dem.avl_available_p (prev, curr_info))
			    {
			      all_compatible = false;
			      break;
			    }
			  if (prev.ime_required_p ())
			    {
			      if (!(prev.get_ime_req ()
				    == curr_info.get_ime_req ()))
				{
				  all_compatible = false;
				  break;
				}
			      any_ime_satisfies = true;
			    }
			}
		      reach_satisfies
			= all_compatible && any_ime_satisfies;
		    }

		  if (reach_satisfies)
		    {
		      if (dump_file)
			fprintf (dump_file,
				 "\n  Skip vsetvl emit before insn %d: "
				 "reaching IME def + WARL-compatible "
				 "preservers satisfy demand\n",
				 insn->uid ());
		    }
		  else
		    {
		      if (dump_file)
			{
			  fprintf (dump_file,
				   "\n  Insert vsetvl info before insn %d: ",
				   insn->uid ());
			  curr_info.dump (dump_file, "    ");
			}
		      insert_vsetvl_insn (EMIT_BEFORE, curr_info);
		    }
		  prior_emission_in_bb = true;
		}
	    }
	}
    }

  for (const vsetvl_info &item : m_delete_list)
    {
      gcc_assert (vsetvl_insn_p (item.get_insn ()->rtl ()));
      remove_vsetvl_insn (item.get_insn ()->rtl ());
    }

  /* Insert vsetvl info that was not deleted after lift up.  */
  for (const bb_info *bb : crtl->ssa->bbs ())
    {
      const vsetvl_block_info &block_info = get_block_info (bb);
      if (!block_info.has_info ())
	continue;

      const vsetvl_info &footer_info = block_info.get_exit_info ();

      if (footer_info.delete_p ())
	continue;

      edge eg;
      edge_iterator eg_iterator;
      FOR_EACH_EDGE (eg, eg_iterator, bb->cfg_bb ()->succs)
	{
	  gcc_assert (!(eg->flags & EDGE_ABNORMAL));
	  if (dump_file)
	    {
	      fprintf (
		dump_file,
		"\n  Insert missed vsetvl info at edge(bb %u -> bb %u): ",
		eg->src->index, eg->dest->index);
	      footer_info.dump (dump_file, "    ");
	    }
	  start_sequence ();
	  insert_vsetvl_insn (EMIT_DIRECT, footer_info);
	  rtx_insn *rinsn = end_sequence ();
	  default_rtl_profile ();
	  insert_insn_on_edge (rinsn, eg);
	  need_commit = true;
	}
    }

  /* m_insert vsetvl as LCM suggest. */
  for (int ed = 0; ed < NUM_EDGES (m_edges); ed++)
    {
      edge eg = INDEX_EDGE (m_edges, ed);
      sbitmap i = m_insert[ed];
      if (bitmap_count_bits (i) != 1)
	/* For code with infinite loop (e.g. pr61634.c), The data flow is
	   completely wrong.  */
	continue;

      unsigned expr_index = bitmap_first_set_bit (i);
      const vsetvl_info &info = *m_exprs[expr_index];
      gcc_assert (info.valid_p ());
      if (dump_file)
	{
	  fprintf (dump_file,
		   "\n  Insert vsetvl info at edge(bb %u -> bb %u): ",
		   eg->src->index, eg->dest->index);
	  info.dump (dump_file, "    ");
	}
      rtl_profile_for_edge (eg);
      start_sequence ();

      insert_vsetvl_insn (EMIT_DIRECT, info);
      rtx_insn *rinsn = end_sequence ();
      default_rtl_profile ();

      /* We should not get an abnormal edge here.  */
      gcc_assert (!(eg->flags & EDGE_ABNORMAL));
      need_commit = true;
      insert_insn_on_edge (rinsn, eg);
    }

  if (need_commit)
    commit_edge_insertions ();
}

void
pre_vsetvl::cleanup ()
{
  remove_avl_operand ();
  remove_unused_dest_operand ();
  remove_vsetvl_pre_insns ();
}

void
pre_vsetvl::remove_avl_operand ()
{
  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, cfun)
    FOR_BB_INSNS (cfg_bb, rinsn)
      if (NONDEBUG_INSN_P (rinsn) && has_vl_op (rinsn)
	  && REG_P (get_vl (rinsn)))
	{
	  rtx avl = get_vl (rinsn);
	  if (count_regno_occurrences (rinsn, REGNO (avl)) == 1)
	    {
	      rtx new_pat;
	      if (fault_first_load_p (rinsn))
		new_pat
		  = simplify_replace_rtx (PATTERN (rinsn), avl, const0_rtx);
	      else
		{
		  rtx set = single_set (rinsn);
		  rtx src
		    = simplify_replace_rtx (SET_SRC (set), avl, const0_rtx);
		  rtx new_set = gen_rtx_SET (SET_DEST (set), src);
		  /* If the pattern is a PARALLEL (e.g. microscaled IME
		     patterns with a (use ...) side-effect), preserve the
		     PARALLEL structure and replace only the SET inside.  */
		  rtx pat = PATTERN (rinsn);
		  if (GET_CODE (pat) == PARALLEL)
		    {
		      rtvec vec = rtvec_alloc (XVECLEN (pat, 0));
		      for (int i = 0; i < XVECLEN (pat, 0); i++)
			{
			  rtx elt = XVECEXP (pat, 0, i);
			  RTVEC_ELT (vec, i) = (elt == set) ? new_set : elt;
			}
		      new_pat = gen_rtx_PARALLEL (VOIDmode, vec);
		    }
		  else
		    new_pat = new_set;
		}
	      if (dump_file)
		{
		  fprintf (dump_file, "  Cleanup insn %u's avl operand:\n",
			   INSN_UID (rinsn));
		  print_rtl_single (dump_file, rinsn);
		}
	      validate_change_or_fail (rinsn, &PATTERN (rinsn), new_pat, false);
	    }
	}
}

void
pre_vsetvl::remove_unused_dest_operand ()
{
  df_analyze ();
  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, cfun)
    FOR_BB_INSNS (cfg_bb, rinsn)
      if (NONDEBUG_INSN_P (rinsn) && vsetvl_insn_p (rinsn))
	{
	  rtx vl = get_vl (rinsn);
	  vsetvl_info info = vsetvl_info (rinsn);
	  /* IME vsetvls are register-form sequences; rewriting them
	     in place via validate_change_or_fail is not supported.  */
	  if (info.ime_required_p ())
	    continue;
	  if (has_no_uses (cfg_bb, rinsn, REGNO (vl)))
	    if (!info.has_vlmax_avl ())
	      {
		rtx new_pat = info.get_vsetvl_pat (true);
		if (dump_file)
		  {
		    fprintf (dump_file,
			     "  Remove vsetvl insn %u's dest(vl) operand since "
			     "it unused:\n",
			     INSN_UID (rinsn));
		    print_rtl_single (dump_file, rinsn);
		  }
		validate_change_or_fail (rinsn, &PATTERN (rinsn), new_pat,
					 false);
	      }
	}
}

/* Remove all bogus vsetvl_pre instructions.  */
void
pre_vsetvl::remove_vsetvl_pre_insns ()
{
  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, cfun)
    FOR_BB_INSNS (cfg_bb, rinsn)
      if (NONDEBUG_INSN_P (rinsn) && vsetvl_pre_insn_p (rinsn))
	{
	  if (dump_file)
	    {
	      fprintf (dump_file, "  Eliminate vsetvl_pre insn %d:\n",
		       INSN_UID (rinsn));
	      print_rtl_single (dump_file, rinsn);
	    }
	  remove_vsetvl_insn (rinsn);
	}
}

const pass_data pass_data_vsetvl = {
  RTL_PASS,	 /* type */
  "vsetvl",	 /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_MACH_DEP,	 /* tv_id */
  0,		 /* properties_required */
  0,		 /* properties_provided */
  0,		 /* properties_destroyed */
  0,		 /* todo_flags_start */
  0,		 /* todo_flags_finish */
};

class pass_vsetvl : public rtl_opt_pass
{
private:
  void simple_vsetvl ();
  void lazy_vsetvl ();

public:
  pass_vsetvl (gcc::context *ctxt) : rtl_opt_pass (pass_data_vsetvl, ctxt) {}

  /* opt_pass methods: */
  virtual bool gate (function *) final override { return TARGET_VECTOR; }
  virtual unsigned int execute (function *) final override;
}; // class pass_vsetvl

void
pass_vsetvl::simple_vsetvl ()
{
  if (dump_file)
    fprintf (dump_file, "\nEntering Simple VSETVL PASS\n");

  basic_block cfg_bb;
  rtx_insn *rinsn;
  FOR_ALL_BB_FN (cfg_bb, cfun)
    {
      FOR_BB_INSNS (cfg_bb, rinsn)
	{
	  if (!NONDEBUG_INSN_P (rinsn))
	    continue;
	  if (has_vtype_op (rinsn))
	    {
	      const auto &info = vsetvl_info (rinsn);
	      if (info.ime_required_p ())
		{
		  /* Same lambda-elision optimisation as in
		     `pre_vsetvl::insert_ime_vsetvl_insn'.  */
		  if (info.get_ime_req ().lambda_log2 >= 0
		      && lambda_already_established_p (
			   rinsn, info.get_ime_req ().lambda_log2))
		    {
		      if (dump_file)
			fprintf (dump_file,
				 "  Elide IME vsetvl insertion before "
				 "insn %d: lambda=%d already established\n",
				 INSN_UID (rinsn),
				 1 << info.get_ime_req ().lambda_log2);
		      continue;
		    }
		  rtx avl = info.get_avl () ? info.get_avl () : const0_rtx;
		  /* See `pre_vsetvl::insert_ime_vsetvl_insn' for why
		     dest_vl differs between the runtime and static paths
		     (x0 vs prologue temp).  */
		  rtx dest_vl;
		  rtx pat;
		  if (info.get_ime_req ().lambda_log2 == -1)
		    {
		      /* Runtime path: see comment in
			 `pre_vsetvl::insert_ime_vsetvl_insn'.  */
		      extract_insn_cached (rinsn);
		      int idx = get_attr_ime_vtype_op_idx (rinsn);
		      if (idx == INVALID_ATTRIBUTE)
			continue;
		      rtx fvX = recog_data.operand[idx];
		      if (!REG_P (fvX))
			continue;
		      dest_vl = info.has_vl () ? info.get_vl ()
					       : gen_rtx_REG (Pmode, 0);
		      /* See `insert_ime_vsetvl_insn' for why we materialise
			 a const_int AVL into the prologue temp here.  */
		      if (avl != const0_rtx && CONST_INT_P (avl))
			{
			  rtx temp
			    = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
			  emit_insn_before (gen_rtx_SET (temp, avl), rinsn);
			  avl = temp;
			}
		      {
			rtx ime_pack
			  = gen_int_mode (pack_ime_req (info.get_ime_req ()),
					  Pmode);
			pat = gen_vsetvl_ime_reg (Pmode, dest_vl, avl, fvX,
						  ime_pack);
		      }
		    }
		  else
		    {
		      rtx vtype_const = emit_ime_vtype_build (info);
		      if (vtype_const == NULL_RTX)
			continue;  /* sorry() already reported.  */
		      dest_vl
			= info.has_vl ()
			    ? info.get_vl ()
			    : gen_rtx_REG (Pmode,
					   RISCV_PROLOGUE_TEMP_REGNUM);
		      /* See the matching branch in `insert_ime_vsetvl_insn'
			 for why we materialise a const_int AVL into the
			 prologue temp here.  */
		      if (avl != const0_rtx && CONST_INT_P (avl))
			{
			  rtx temp
			    = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
			  emit_insn_before (gen_rtx_SET (temp, avl), rinsn);
			  avl = temp;
			}
		      {
			rtx ime_pack
			  = gen_int_mode (pack_ime_req (info.get_ime_req ()),
					  Pmode);
			pat = gen_vsetvl_ime (Pmode, dest_vl, avl, vtype_const,
					      ime_pack);
		      }
		    }
		  emit_insn_before (pat, rinsn);
		  if (dump_file)
		    fprintf (dump_file,
			     "  Insert IME vsetvl insn before insn %d\n",
			     INSN_UID (rinsn));
		  continue;
		}
	      rtx pat = info.get_vsetvl_pat ();
	      emit_insn_before (pat, rinsn);
	      if (dump_file)
		{
		  fprintf (dump_file, "  Insert vsetvl insn before insn %d:\n",
			   INSN_UID (rinsn));
		  print_rtl_single (dump_file, PREV_INSN (rinsn));
		}
	    }
	}
    }

  if (dump_file)
    fprintf (dump_file, "\nEmit missing read_vl()s for fault-only-first "
	     "loads\n");
  emit_fof_read_vls ();
}

/* Lazy vsetvl insertion for optimize > 0. */
void
pass_vsetvl::lazy_vsetvl ()
{
  if (dump_file)
    fprintf (dump_file, "\nEntering Lazy VSETVL PASS\n\n");

  pre_vsetvl pre = pre_vsetvl ();

  if (dump_file)
    fprintf (dump_file, "\nPhase 1: Fuse local vsetvl infos.\n\n");
  pre.fuse_local_vsetvl_info ();
  if (dump_file && (dump_flags & TDF_DETAILS))
    pre.dump (dump_file, "phase 1");

  /* Phase 2:  Fuse header and footer vsetvl infos between basic blocks.  */
  if (dump_file)
    fprintf (dump_file, "\nPhase 2: Lift up vsetvl info.\n\n");
  if (vsetvl_strategy != VSETVL_OPT_NO_FUSION)
    {
      bool changed = true;
      int fused_count = 0;
      do
	{
	  if (dump_file)
	    fprintf (dump_file, "  Try lift up %d.\n\n", fused_count);
	  changed = pre.earliest_fuse_vsetvl_info (fused_count);
	  fused_count += 1;
      } while (changed);
    }
  if (dump_file && (dump_flags & TDF_DETAILS))
    pre.dump (dump_file, "phase 2");

  /* Phase 3: Reducing redundant vsetvl infos using LCM.  */
  if (dump_file)
    fprintf (dump_file, "\nPhase 3: Reduce global vsetvl infos.\n\n");
  pre.pre_global_vsetvl_info ();
  if (dump_file && (dump_flags & TDF_DETAILS))
    pre.dump (dump_file, "phase 3");

  /* Phase 4: Insert, modify and remove vsetvl insns.  */
  if (dump_file)
    fprintf (dump_file,
	     "\nPhase 4: Insert, modify and remove vsetvl insns.\n\n");
  pre.emit_vsetvl ();

  /* Phase 4b: Emit read_vl for fault-only-first loads with VL output
     register.  */
  if (dump_file)
    fprintf (dump_file, "\nPhase 4b: Emit missing read_vl()s for "
	     "fault-only-first loads\n");
  emit_fof_read_vls ();

  /* Phase 5: Cleanup */
  if (dump_file)
    fprintf (dump_file, "\nPhase 5: Cleanup\n\n");
  pre.cleanup ();

  pre.finish ();
}

/* Forward intra-block scan from PLACEHOLDER for the first lambda-USE
   consumer reachable before any non-placeholder vtype DEF/KILL.
   Returns the consumer rtx_insn (whose vtype demand drives the
   placeholder's rewrite) or nullptr if no such consumer exists.
   Other `vsetvl_no_side_effects' placeholders along the way are
   skipped: each gets rewritten independently when this function
   visits it, so they do not cut the search short.

   Bounded by MAX_WALK steps to defend against pathological blocks.  */

static rtx_insn *
find_lambda_use_consumer (rtx_insn *placeholder)
{
  basic_block bb = BLOCK_FOR_INSN (placeholder);
  if (!bb)
    return nullptr;

  const int max_walk = 64;
  int steps = 0;
  for (rtx_insn *p = NEXT_INSN (placeholder);
       p && steps < max_walk;
       p = NEXT_INSN (p), ++steps)
    {
      if (BLOCK_FOR_INSN (p) != bb)
	return nullptr;
      if (!NONDEBUG_INSN_P (p))
	continue;

      if (insn_uses_lambda_p (p))
	return p;

      int code = INSN_CODE (p);
      if (code == CODE_FOR_vsetvldi_no_side_effects
	  || code == CODE_FOR_vsetvlsi_no_side_effects)
	continue;

      lambda_state s = classify_lambda_effect (p);
      if (s.kind == lambda_state_kind::DEFINED
	  || s.kind == lambda_state_kind::DEFINED_RUNTIME
	  || s.kind == lambda_state_kind::KILLED)
	return nullptr;
    }
  return nullptr;
}

/* Replace the `vsetvl<mode>_no_side_effects' PLACEHOLDER with a
   lambda-preserving form chosen by REACH:
     - DEFINED(N):        `gen_vsetvl_ime' with a const_int vtype that
			  bakes in the lambda field (enc = N+1).  After
			  the rewrite, the placeholder writes the full
			  vtype atomically including lambda=N.
     - DEFINED_RUNTIME:   `gen_riscv_vsetvl_preserve_lambda' which
			  reads vtype.lambda at run time, ORs it into the
			  static (SEW/LMUL/ta/ma) bits, then vsetvls.

   The placeholder's own (SEW, LMUL, ta, ma) is the intrinsic
   expander's arbitrary `e8,mfN' choice (see the comment at
   `expand_select_vl' in riscv-v.cc:4574 -- "since vsetvl only demand
   ratio, we let VSETVL PASS to optimize it").  Using those bits here
   would emit a preserve_lambda with the wrong SEW reaching the
   downstream lambda-USE consumer -- which for IME tile load/store
   ops causes the hardware to compute the in-register stride against
   the wrong element width and silently corrupt the tile geometry.

   So instead we derive (SEW, LMUL, ta, ma) from the downstream
   CONSUMER's mode and attributes -- that is the vtype the consumer
   actually demands.  Combined with `parse_insn's decoding of
   preserve_lambda's vtype operand (Commit 2 of this series), the
   LCM then sees this rewritten preserve_lambda as an available
   vtype source for the consumer and elides any redundant second
   lambda-preserving emission.

   The dest_vl pseudo is preserved so downstream consumers still see
   the correct VL register.  */

static void
rewrite_placeholder_lambda_preserving (rtx_insn *placeholder,
				       rtx_insn *consumer,
				       const lambda_state &reach)
{
  extract_insn_cached (placeholder);
  rtx dest_vl = recog_data.operand[0];
  rtx avl = recog_data.operand[1];

  /* Extract (SEW, LMUL, ta, ma) from the downstream consumer's
     attributes -- NOT from the placeholder's own operands.  */
  int sew = (int) ::get_sew (consumer);
  vlmul_type vlmul = ::get_vlmul (consumer);
  bool ta = tail_agnostic_p (consumer);
  bool ma = mask_agnostic_p (consumer);

  int xlen = GET_MODE_BITSIZE (Pmode);
  HOST_WIDE_INT static_vtype = 0;
  static_vtype |= ((HOST_WIDE_INT) vlmul) & 0x7;
  static_vtype |= (HOST_WIDE_INT) ime_vsew_encoding (sew) << 3;
  if (ta)
    static_vtype |= (HOST_WIDE_INT) 1 << 6;
  if (ma)
    static_vtype |= (HOST_WIDE_INT) 1 << 7;

  /* The IME and preserve patterns accept only register-or-zero for AVL.
     Materialise a const_csr immediate into the prologue temp first.  */
  if (avl != const0_rtx && CONST_INT_P (avl))
    {
      rtx temp = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
      emit_insn_before (gen_rtx_SET (temp, avl), placeholder);
      avl = temp;
    }

  /* Both helper patterns declare `=&r' (early-clobber) for the dest_vl
     output: the asm template writes the dest before reading the AVL
     input, so dest_vl and AVL must not share a register.  Post-reload
     RA cannot reassign for us, so when the placeholder's RA happened
     to bind both operands to the same hard reg, copy the AVL into the
     prologue temp first.  */
  if (REG_P (avl) && REG_P (dest_vl) && REGNO (avl) == REGNO (dest_vl))
    {
      rtx temp = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP_REGNUM);
      emit_insn_before (gen_rtx_SET (temp, avl), placeholder);
      avl = temp;
    }

  rtx pat;
  if (reach.kind == lambda_state_kind::DEFINED)
    {
      int enc = reach.known_log2 + 1;
      HOST_WIDE_INT vtype
	= static_vtype | ((HOST_WIDE_INT) enc << (xlen - 4));
      /* Lambda-preserving emission for a non-IME consumer: pass 0 as the
	 IME-demand operand (sew=0 sentinel decodes to "no IME demand" in
	 parse_insn).  */
      pat = gen_vsetvl_ime (Pmode, dest_vl, avl,
			    gen_int_mode (vtype, Pmode),
			    const0_rtx);
    }
  else
    {
      gcc_assert (reach.kind == lambda_state_kind::DEFINED_RUNTIME);
      /* The preserve_lambda pattern needs a scratch GPR for the
	 mask-build/static-bits chain.  Allocating fresh pseudos is not
	 possible post-reload, so reuse `t1' -- distinct from dest_vl
	 (whose RA could leave it on `t0' if we copied AVL there above)
	 and from AVL.  */
      rtx scratch = gen_rtx_REG (Pmode, RISCV_PROLOGUE_TEMP2_REGNUM);
      pat = gen_riscv_vsetvl_preserve_lambda (
	Pmode, dest_vl, avl, gen_int_mode (static_vtype, Pmode), scratch);
    }

  emit_insn_before (pat, placeholder);
  delete_insn (placeholder);
}

/* Post-pass scan that closes the placeholder-vs-install reorder hazard.

   The vsetvl pass does not DEF VTYPE_REGNUM on `vsetvl_no_side_effects'
   placeholders pre-split, so scheduling can move such a placeholder
   past a `riscv_vsetlambda_install'.  When the placeholder later splits
   into a real vsetvli at epilogue_completed, that vsetvli sits between
   the install and a downstream lambda consumer and WARL-clobbers
   vtype.lambda.

   Walk every block, maintaining a running lambda reach state (seeded
   by the lambda dataflow's entry state and updated per insn via
   `classify_lambda_effect').  When we encounter a placeholder under
   a DEFINED / DEFINED_RUNTIME reach and a lambda-USE follows in the
   same block before any other vtype DEF/KILL, rewrite the placeholder
   into a lambda-preserving form (vsetvl_ime carrying lambda=N for
   DEFINED, or vsetvl_preserve_lambda for DEFINED_RUNTIME).  */

static void
fixup_lambda_clobbering_placeholders ()
{
  if (!g_lambda_df)
    return;

  basic_block bb;
  unsigned rewrites = 0;
  FOR_EACH_BB_FN (bb, cfun)
    {
      lambda_state cur = g_lambda_df->entry_state_of (bb);

      rtx_insn *insn, *next;
      for (insn = BB_HEAD (bb);
	   insn && insn != NEXT_INSN (BB_END (bb));
	   insn = next)
	{
	  next = NEXT_INSN (insn);
	  if (!NONDEBUG_INSN_P (insn))
	    continue;

	  int code = INSN_CODE (insn);
	  bool is_placeholder
	    = (code == CODE_FOR_vsetvldi_no_side_effects
	       || code == CODE_FOR_vsetvlsi_no_side_effects);

	  /* Only the DEFINED_RUNTIME case needs the fixup: with a static
	     lambda the pass emits a `vsetvl_ime' before each lambda-USE
	     consumer that carries lambda=N as a const, atomically
	     re-establishing lambda after any intervening clobber.  With a
	     runtime lambda the consumer's `vsetvl_preserve_lambda' reads
	     vtype.lambda *at run time* -- if a plain vsetvli has clobbered
	     it between install and consumer, the read sees the clobbered
	     value, not the user's.  We must therefore replace the
	     placeholder itself with a lambda-preserving form for the
	     runtime case only.  */
	  rtx_insn *consumer = nullptr;
	  if (is_placeholder
	      && cur.kind == lambda_state_kind::DEFINED_RUNTIME
	      && (consumer = find_lambda_use_consumer (insn)) != nullptr)
	    {
	      if (dump_file)
		fprintf (dump_file,
			 "Fixup placeholder vsetvli insn %d: rewriting "
			 "as vsetvl_preserve_lambda (runtime lambda) "
			 "with vtype from consumer insn %d\n",
			 INSN_UID (insn), INSN_UID (consumer));
	      rewrite_placeholder_lambda_preserving (insn, consumer, cur);
	      ++rewrites;
	      /* The rewritten insn preserves lambda, so CUR stays
		 valid as DEFINED_RUNTIME for the remainder of the block.  */
	      continue;
	    }

	  lambda_state s = classify_lambda_effect (insn);
	  switch (s.kind)
	    {
	    case lambda_state_kind::NOT_TOUCHED:
	    case lambda_state_kind::USE_ONLY:
	      break;
	    case lambda_state_kind::DEFINED:
	    case lambda_state_kind::DEFINED_RUNTIME:
	    case lambda_state_kind::KILLED:
	      cur = s;
	      break;
	    }
	}
    }

  if (dump_file && rewrites)
    fprintf (dump_file,
	     "Fixup: rewrote %u placeholder%s to preserve user lambda\n",
	     rewrites, rewrites == 1 ? "" : "s");
}

/* Main entry point for this pass.  */
unsigned int
pass_vsetvl::execute (function *)
{
  if (n_basic_blocks_for_fn (cfun) <= 0)
    return 0;

  /* PRE-SPLIT lambda-clobber placeholder fixup.

     `vsetvl<mode>_no_side_effects' placeholders do not DEF VTYPE_REGNUM
     pre-split, so the lambda dataflow sees them as NOT_TOUCHED and the
     reach at the placeholder still reflects the user's most recent
     `__riscv_vsetlambda' install.  Once split (a few lines down), each
     placeholder becomes a real vsetvli that WARL-clobbers lambda; a
     downstream IME tile/mmacc consumer then loses the user's lambda.

     We must therefore run the dataflow AND rewrite the placeholders
     BEFORE `split_all_insns' makes them indistinguishable from any
     other plain vsetvli.  After rewrite, the affected placeholders
     are `vsetvl_ime' (carrying lambda=N as a const) or
     `vsetvl_preserve_lambda' (RMW that transplants the live lambda),
     both of which split or expand to lambda-safe code.  */
  if (has_vector_insn (cfun) && riscv_track_user_lambda)
    {
      lambda_dataflow pre_df;
      if (dump_file)
	fprintf (dump_file,
		 "\nLambda dataflow (pre-split): scanning for placeholders that "
		 "would WARL-clobber a live user lambda\n");
      pre_df.run ();
      g_lambda_df = &pre_df;
      fixup_lambda_clobbering_placeholders ();
      g_lambda_df = nullptr;
    }

  /* The RVV instruction may change after split which is not a stable
     instruction. We need to split it here to avoid potential issue
     since the VSETVL PASS is insert before split PASS.  */
  split_all_insns ();

  /* Early return for there is no vector instructions.  */
  if (!has_vector_insn (cfun))
    return 0;

  /* Run the lambda dataflow before either emission strategy.  Its
     per-block entry states feed cross-block elision in
     `lambda_already_established_p'; its dead-install list drives the
     vsetlambda DCE pass run immediately afterwards.  Both consumers
     are gated by `-mtrack-user-lambda' (default on).  */
  lambda_dataflow df;
  if (riscv_track_user_lambda)
    {
      if (dump_file)
	fprintf (dump_file,
		 "\nLambda dataflow: running CFG-level analysis (DCE+cross-block elision)\n");
      df.run ();
      g_lambda_df = &df;
      if (dump_file)
	fprintf (dump_file,
		 "Lambda dataflow: %u redundant vsetlambda install%s identified\n",
		 df.dead_installs ().length (),
		 df.dead_installs ().length () == 1 ? "" : "s");

      /* Remove vsetlambda installs whose target lambda is already
	 established at their insertion point.  This is the cross-block
	 generalisation of the local DCE: a single user setter that
	 dominates the function entry block makes every subsequent
	 in-loop vsetlambda(N) redundant.  */
      if (!df.dead_installs ().is_empty ())
	{
	  if (dump_file)
	    fprintf (dump_file,
		     "\nLambda DCE: removing %u redundant vsetlambda "
		     "install%s\n",
		     df.dead_installs ().length (),
		     df.dead_installs ().length () == 1 ? "" : "s");
	  for (rtx_insn *ins : df.dead_installs ())
	    {
	      if (dump_file)
		{
		  fprintf (dump_file, "  delete redundant install insn %d:\n",
			   INSN_UID (ins));
		  print_rtl_single (dump_file, ins);
		}
	      delete_insn (ins);
	    }
	}
    }

  if (!optimize || vsetvl_strategy == VSETVL_SIMPLE)
    simple_vsetvl ();
  else
    lazy_vsetvl ();

  g_lambda_df = nullptr;
  return 0;
}

rtl_opt_pass *
make_pass_vsetvl (gcc::context *ctxt)
{
  return new pass_vsetvl (ctxt);
}
