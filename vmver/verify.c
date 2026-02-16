#ifndef VERIFY_H
#define VERIFY_H

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "callstack.h"
#include "disasm.h"

typedef uint32_t symoff; // Symbol offset

static const symoff FUNC_UNDEF = (symoff)~0u;

extern size_t __gc_stack_top, __gc_stack_bottom;

typedef enum local_idx_e {
  MAX_DEPTH_IDX = 0,
  BEGIN_OFF_IDX = 1,
  MAX_IDX
} local_idx;

static inline bool is_jump(bytecode op) {
  return op == OP_JMP || op == OP_CJMPZ || op == OP_CJMPNZ;
}

static inline bool is_terminal(bytecode op) {
  return op == OP_JMP || op == OP_END || op == OP_RET || op == OP_FAIL ||
         op == OP_STOP;
}

static inline int join_paths(symoff dstoff, stkdepth *const stkdepth_arr,
                             stkdepth srcdepth) {
  if (stkdepth_arr[dstoff] == FUNC_UNDEF) {
    stkdepth_arr[dstoff] = srcdepth;
    RETURN_IF_ERROR(callstack_push_operand(csval_imm(dstoff)));
  } else {
    if (stkdepth_arr[dstoff] != srcdepth) {
      fprintf(stderr, "%x: Stack depth not match in pathes join! (%d != %d)",
              dstoff, stkdepth_arr[dstoff], srcdepth);
      return 1;
    }
  }
  return 0;
}

int verify(bytefile *bytefile) {
  const size_t code_size = get_code_size(bytefile);
  const size_t total_bytes =
      code_size * sizeof(symoff) /* stack_depth at bottom */ +
      /* Из расчета что самый длинный стек будет если мы последовательно
         сделаем call -> call -> call и так для всех символов*/
      (CFRAME_HEADER_SZ + CFRAME_DATA_SZ(MAX_IDX)) * code_size;

  /* Create call stack */
  cs_init(bytefile->global_area_size, total_bytes);
  stkdepth *stack_depth = (stkdepth *)((char *)__gc_stack_top + total_bytes -
                                       code_size * sizeof(stkdepth));

  // Take pointer to stack_depth (bottom of callstack)
  // __gc_stack_bottom не дойдет тк сможем запушить ровно до stack_depth адреса
  for (size_t i = 0; i < code_size; i++)
    stack_depth[i] = FUNC_UNDEF;

  // Initialize root cframe with
  // 1 local (max depth needed to cover all bytecodes)
  RETURN_IF_ERROR(cs_alloc_locals(MAX_IDX, 0));
  RETURN_IF_ERROR(callstack_set_local(MAX_DEPTH_IDX, csval_imm(0)));
  RETURN_IF_ERROR(callstack_set_local(BEGIN_OFF_IDX, csval_imm(FUNC_UNDEF)));

  // DFS
  // Fill workset with publics
  for (size_t i = 0; i < get_public_count(bytefile); ++i) {
    symoff offset = get_public_offset(bytefile, i);
    if (offset >= code_size) {
      fprintf(stderr, "Invalid public symbol offset %x!", offset);
      return 1;
    }
    RETURN_IF_ERROR(callstack_push_operand(csval_imm(offset)));
    stack_depth[offset] = 0;
  }

  // empty the workset
  while (__cs_nframes > 0) {
    if (__cs_noperands() == 0) {
      if (__cs_nframes == 1)
        break;

      csval_t md, bo, rd;
      RETURN_IF_ERROR(callstack_get_local(MAX_DEPTH_IDX, &md));
      RETURN_IF_ERROR(callstack_get_local(BEGIN_OFF_IDX, &bo));

      symoff begin_off = (symoff)UNBOX(bo.val);
      stkdepth max_depth = (stkdepth)UNBOX(md.val);

      set_arg2_bighalf(bytefile, begin_off, max_depth);

      uint32_t ret_off;
      RETURN_IF_ERROR(cs_pop_frame(&ret_off));

      continue;
    }

    csval_t csval_offset;
    RETURN_IF_ERROR(callstack_pop_operand(&csval_offset));

    // Take info of current bytecode
    bytecode op;
    stkdepth inc, dec;
    symoff offset = UNBOX(csval_offset.val);
    stkdepth stk_at_entry = stack_depth[offset];

    int length =
        disassemble_instruction(stdin, bytefile, offset, &op, &inc, &dec);
    if (length <= 0) {
      fprintf(stderr, "%x: Invalid instruction length", offset);
      return 1;
    }
    if (offset + length > code_size) {
      fprintf(stderr, "%x: Unexpected end of code!", offset + length);
      return 1;
    }
    if (stk_at_entry < dec) {
      fprintf(stderr, "%x: Stack underflow!", offset);
      return 1;
    }
    stkdepth stk_at_next = stk_at_entry + inc - dec;

    // Update max depth
    csval_t local;
    RETURN_IF_ERROR(callstack_get_local(MAX_DEPTH_IDX, &local));
    stkdepth frame_max_depth = UNBOX(local.val);
    if (stk_at_next > frame_max_depth)
      RETURN_IF_ERROR(
          callstack_set_local(MAX_DEPTH_IDX, csval_imm(stk_at_next)));

    // Process ops
    if (is_jump(op)) {
      int32_t target_i = get_arg(bytefile, offset);
      symoff target = (symoff)target_i;
      if (target_i < 0 || target >= code_size) {
        fprintf(stderr, "%x: Invalid jump/call destination!", offset);
        return 1;
      }

      RETURN_IF_ERROR(join_paths(target, stack_depth, stk_at_next));
    }
    if (!is_terminal(op)) {
      symoff next_offset = offset + length;
      RETURN_IF_ERROR(join_paths(next_offset, stack_depth, stk_at_next));
    }
    if (op == OP_CALL || op == OP_CLOSURE) {
      int32_t begin_i = get_arg(bytefile, offset);
      if (begin_i < 0 || (symoff)begin_i >= code_size) {
        fprintf(stderr, "%x: Invalid call destination!\n", offset);
        return 1;
      }
      symoff resume_off = offset + length;

      RETURN_IF_ERROR(cs_push_frame(resume_off, 0));
      RETURN_IF_ERROR(cs_alloc_locals(MAX_IDX, 0));

      RETURN_IF_ERROR(callstack_set_local(MAX_DEPTH_IDX, csval_imm(0)));
      RETURN_IF_ERROR(callstack_set_local(BEGIN_OFF_IDX, csval_imm(begin_i)));

      RETURN_IF_ERROR(join_paths((symoff)begin_i, stack_depth, 0));
    }
  }

  RETURN_IF_ERROR(cs_reset());
  return 0;
}

#endif