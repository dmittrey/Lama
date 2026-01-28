#ifndef CALLSTACK_H
#define CALLSTACK_H

#include "gc.h"
#include "runtime_common.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "csval.h"
#include "error.h"

#define CALLSTACK_INITIAL_SIZE 64
#define CALLSTACK_MAX_SIZE 32768

#define CSVAL_WORDS 2

/*
Call frame memory layout(RAM):
+----------------------------------+ <- ram_layout
| globals[0..nglobals-1] (csval_t) |
+----------------------------------+
| ...                              |
+----------------------------------+
| arguments (csval_t)              |
+----------------------------------+ <- (SP when push new frame)
| closure | BOX(0)                 | closure ptr or BOX(0)
+----------------------------------+
| ret_off (csval_t)                |
+----------------------------------+
| nargs (csval_t)                  |
+----------------------------------+
| prev_fp (csval_t)                |   // 0 => no prev frame
+----------------------------------+ <- FP
| nlocals (csval_t)                |
+----------------------------------+
| locals[0..nlocals-1] (csval_t)   |   // VM values
+----------------------------------+
| noperands (csval_t)              |
+----------------------------------+
| operands  (csval_t[])            |   // VM values
+----------------------------------+ <- SP
| free                             |
+----------------------------------+
||
|| grow down
\/
*/

extern size_t __gc_stack_top, __gc_stack_bottom;

/* Virtual regs */
extern size_t __cs_fp; /* slot index of nlocals for current frame */
extern size_t __cs_sp; /* slot index of next free entry */

/* Internal */
extern size_t __cs_cap;     /* allocated capacity (csval slots) */
extern size_t __cs_nframes; /* prevent underflow */
extern size_t __cs_nglob;   /* number of globals */
extern aint *__cs_ram_layout;

/* Interact with RAM layout */
static inline size_t __csval_slot_to_ram_idx(size_t slot) {
  return slot * (size_t)CSVAL_WORDS;
}
static inline error_code_e __cs_slot_write(size_t slot, csval_t v) {
  size_t w = __csval_slot_to_ram_idx(slot);
  __cs_ram_layout[w] = BOX((aint)v.ty); // to skip during GC scan
  __cs_ram_layout[w + 1] = v.val;
  return ERROR_NONE;
}
static inline csval_t __cs_slot_read(size_t slot) {
  size_t w = __csval_slot_to_ram_idx(slot);
  aint type_word = __cs_ram_layout[w];
  return (csval_t){.ty = (csval_type_e)UNBOX(type_word),
                   .val = __cs_ram_layout[w + 1]};
}

/* Link with csval interface */
static inline error_code_e __cs_write_imm(size_t slot, uint32_t value) {
  return __cs_slot_write(slot, csval_imm(value));
}
static inline error_code_e __cs_read_imm_slot(size_t slot, uint32_t *out) {
  csval_t v = __cs_slot_read(slot);
  if (v.ty != CS_IMM)
    return ERROR_NOT_IMM;
  *out = (uint32_t)UNBOX(v.val);
  return ERROR_NONE;
}

/* Callstack layer helpers */
/* Amount of vals to size of this area in stack */
static inline size_t __nvals_to_size(size_t nwords) {
  return nwords * (size_t)CSVAL_WORDS * sizeof(aint);
}
static inline void __gc_sync() {
  __gc_stack_top = (size_t)__cs_ram_layout;
  __gc_stack_bottom =
      (size_t)(__cs_ram_layout + __csval_slot_to_ram_idx(__cs_sp));
}
static inline error_code_e __cs_ensure_capacity(size_t required_slots) {
  if (required_slots <= __cs_cap) {
    return ERROR_NONE;
  }
  if (required_slots > CALLSTACK_MAX_SIZE) {
    return ERROR_STACK_OVERFLOW;
  }

  size_t new_cap = __cs_cap;
  while (new_cap < required_slots && new_cap < CALLSTACK_MAX_SIZE) {
    new_cap *= 2;
  }
  if (new_cap > CALLSTACK_MAX_SIZE) {
    new_cap = CALLSTACK_MAX_SIZE;
  }
  if (new_cap < required_slots) {
    return ERROR_STACK_OVERFLOW;
  }

  aint *new_layout = (aint *)realloc(
      __cs_ram_layout, new_cap * (size_t)CSVAL_WORDS * sizeof(aint));
  if (!new_layout) {
    fprintf(stderr, "Not enough memory to realloc callstack with size %lu\n",
            (unsigned long)new_cap);
    free(__cs_ram_layout);
    return ERROR_NOT_ENOUGH_MEMORY;
  }

  __cs_ram_layout = new_layout;
  __cs_cap = new_cap;
  __gc_sync();
  return ERROR_NONE;
}
static inline error_code_e __cs_push_slot(csval_t v) {
  RETURN_IF_ERROR(__cs_ensure_capacity(__cs_sp + 1));
  RETURN_IF_ERROR(__cs_slot_write(__cs_sp, v));
  __cs_sp++;
  __gc_sync();
  return ERROR_NONE;
}
static inline error_code_e __cs_pop_slot(csval_t *ret) {
  if (__cs_sp == 0) {
    return ERROR_OPND_STACK_UNDERFLOW;
  }
  __cs_sp--;
  *ret = __cs_slot_read(__cs_sp);
  __gc_sync();
  return ERROR_NONE;
}

/* Segments (csval slots) */
static inline size_t __cs_globs_base_idx() { return 0; } /* layout start */
static inline size_t __cs_nlocs_base_idx() { return __cs_fp; }
static inline size_t __cs_nlocs() {
  uint32_t nlocs;
  if (!__cs_nframes) // First frame TODO erase?
    return 0;
  if (__cs_read_imm_slot(__cs_nlocs_base_idx(), &nlocs))
    return 0;
  return nlocs;
}
// Reverse direction
static inline size_t __cs_prevfp_base_idx() { return __cs_fp - 1; }
static inline size_t __cs_nargs_base_idx() { return __cs_fp - 2; }
static inline size_t cs_nargs() {
  uint32_t nargs;
  if (!__cs_nframes) // First frame TODO erase?
    return 0;
  if (__cs_read_imm_slot(__cs_nargs_base_idx(), &nargs))
    return 0;
  return nargs;
}
static inline size_t __cs_retoff_base_idx() { return __cs_fp - 3; }
static inline size_t __cs_clos_base_idx() { return __cs_fp - 4; }
static inline aint cs_clos() {
  csval_t clos_val;
  if (!__cs_nframes) // First frame TODO erase?
    return BOX(0);
  clos_val = __cs_slot_read(__cs_clos_base_idx());
  return csval_to_aint(clos_val);
}
static inline size_t __cs_args_base_idx() {
  return __cs_clos_base_idx() - cs_nargs();
}
// Forward direction
static inline size_t __cs_locs_base_idx() { return __cs_fp + 1; }
static inline size_t __cs_noperands_base_idx() {
  return __cs_locs_base_idx() + __cs_nlocs();
}
static inline size_t __cs_noperands() {
  uint32_t nopnds;
  if (!__cs_nframes) // First frame TODO erase?
    return 0;
  if (__cs_read_imm_slot(__cs_noperands_base_idx(), &nopnds))
    return 0;
  return nopnds;
}
static inline size_t __cs_operands_base_idx() {
  return __cs_noperands_base_idx() + 1;
}

/* PUBLIC */
/* Lifecycle */
static error_code_e cs_init(int nglobals) {
  __gc_init();

  /* RAM layout */
  __cs_ram_layout = malloc(__nvals_to_size(CALLSTACK_INITIAL_SIZE));
  if (!__cs_ram_layout) {
    fprintf(stderr, "callstack: unable to allocate size=%lu\n",
            __nvals_to_size(CALLSTACK_INITIAL_SIZE));
    return ERROR_NOT_ENOUGH_MEMORY;
  }

  /* Virt regs */
  __cs_fp = __nvals_to_size(nglobals);
  __cs_sp = __cs_fp; // Start callstack from global area end

  /* Internal */
  __cs_cap = (size_t)CALLSTACK_INITIAL_SIZE;
  __cs_nframes = 0;
  __cs_nglob = nglobals;

  __gc_sync();
  return ERROR_NONE;
}
static error_code_e cs_shutdown() {
  free(__cs_ram_layout);
  return ERROR_NONE;
}

/* Frame operations */
static error_code_e cs_push_cframe(aint closure, uint32_t ret_off,
                                   uint32_t nargs) {
  if (!((UNBOXED(closure) && UNBOX(closure) == 0) || !UNBOXED(closure)))
    return ERROR_NOT_VALID_CLOSURE;

  if (nargs > 0) {
    uint32_t noperands_ = __cs_noperands();
    if (nargs > noperands_) {
      return ERROR_OPND_STACK_UNDERFLOW;
    }
    /* Detach args from operand stack without moving slots. */
    RETURN_IF_ERROR(
        __cs_write_imm(__cs_noperands_base_idx(), noperands_ - nargs));
  }

  /* Closure segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_from_aint(closure)));

  /* Return address segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(ret_off)));

  /* Args segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(nargs)));

  /* Prolog */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(__cs_fp)));
  __cs_fp = __cs_sp;
  __cs_nframes++;
  return ERROR_NONE;
}
static error_code_e cs_push_frame(uint32_t ret_off, uint32_t nargs) {
  return cs_push_cframe(BOX(0), ret_off, nargs);
}
static error_code_e cs_alloc_locals(uint32_t nlocals) {
  if (!__cs_nframes)
    RETURN_IF_ERROR(cs_push_frame(0, 0));

  /* Locals segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(nlocals)));
  __cs_sp += nlocals; // Reserve space in frame for nlocals

  /* Operands segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(0))); // noperands
  return ERROR_NONE;
}
static error_code_e cs_pop_frame(uint32_t *ret_off) {
  if (!__cs_nframes)
    return ERROR_STACK_UNDERFLOW;

  /* Epilog */
  __cs_sp = __cs_fp;
  csval_t prev_fp_val;
  RETURN_IF_ERROR(__cs_pop_slot(&prev_fp_val));
  aint prev_fp_aint = 0;
  RETURN_IF_ERROR(csval_to_imm_aint(prev_fp_val, &prev_fp_aint));
  __cs_fp = (size_t)UNBOX(prev_fp_aint);
  __cs_nframes--;

  /* Args segment */
  csval_t nargs_val;
  RETURN_IF_ERROR(__cs_pop_slot(&nargs_val));
  aint nargs_aint = 0;
  RETURN_IF_ERROR(csval_to_imm_aint(nargs_val, &nargs_aint));

  /* Return address segment */
  csval_t ret_off_val;
  RETURN_IF_ERROR(__cs_pop_slot(&ret_off_val));
  aint ret_off_aint;
  RETURN_IF_ERROR(csval_to_imm_aint(ret_off_val, &ret_off_aint));
  *ret_off = (uint32_t)UNBOX(ret_off_aint);

  /* Closure segment */
  csval_t closure_val;
  RETURN_IF_ERROR(__cs_pop_slot(&closure_val));

  if (__cs_sp < (size_t)UNBOX(nargs_aint)) {
    return ERROR_STACK_UNDERFLOW;
  }
  __cs_sp -= (size_t)UNBOX(nargs_aint);

  __gc_sync();
  return ERROR_NONE;
}

/* Access arguments and locals */
static error_code_e callstack_get_local(uint32_t index, csval_t *ret) {
  uint32_t nlocals_ = __cs_nlocs();
  if (index >= nlocals_)
    return ERROR_LOCL_IDX_OUT_OF_RANGE;

  size_t base = __cs_locs_base_idx();
  *ret = __cs_slot_read(base + index);
  return ERROR_NONE;
}
static error_code_e callstack_set_local(uint32_t index, csval_t value) {
  uint32_t nlocals_ = __cs_nlocs();
  if (index >= nlocals_)
    return ERROR_LOCL_IDX_OUT_OF_RANGE;

  size_t base = __cs_locs_base_idx();
  return __cs_slot_write(base + index, value);
}
static error_code_e callstack_get_arg(uint32_t index, csval_t *ret) {
  uint32_t nargs_ = cs_nargs();
  if (index >= nargs_)
    return ERROR_ARG_IDX_OUT_OF_RANGE;

  size_t base = __cs_args_base_idx();
  *ret = __cs_slot_read(base + index);
  return ERROR_NONE;
}
static error_code_e callstack_set_arg(uint32_t index, csval_t value) {
  uint32_t nargs_ = cs_nargs();
  if (index >= nargs_)
    return ERROR_ARG_IDX_OUT_OF_RANGE;

  size_t base = __cs_args_base_idx();
  return __cs_slot_write(base + index, value);
}
static error_code_e callstack_get_glob(uint32_t index, csval_t *ret) {
  uint32_t nglobals_ = __cs_nglob;
  if (index >= nglobals_)
    return ERROR_GLOB_IDX_OUT_OF_RANGE;

  size_t base = __cs_globs_base_idx();
  *ret = __cs_slot_read(base + index);
  return ERROR_NONE;
}
static error_code_e callstack_set_glob(uint32_t index, csval_t value) {
  uint32_t nglobals_ = __cs_nglob;
  if (index >= nglobals_)
    return ERROR_GLOB_IDX_OUT_OF_RANGE;

  size_t base = __cs_globs_base_idx();
  return __cs_slot_write(base + index, value);
}

/* Operands stack */
static error_code_e callstack_pop_operand(csval_t *ret) {
  uint32_t noperands_ = __cs_noperands();
  if (noperands_ == 0)
    return ERROR_OPND_STACK_UNDERFLOW;

  RETURN_IF_ERROR(__cs_pop_slot(ret));
  RETURN_IF_ERROR(__cs_write_imm(__cs_noperands_base_idx(), noperands_ - 1));
  return ERROR_NONE;
}
static error_code_e callstack_push_operand(csval_t value) {
  uint32_t noperands_ = __cs_noperands();
  RETURN_IF_ERROR(__cs_push_slot(value));
  RETURN_IF_ERROR(__cs_write_imm(__cs_noperands_base_idx(), noperands_ + 1));
  return ERROR_NONE;
}
static error_code_e callstack_pop_n_operands(uint32_t n, csval_t *ret) {
  uint32_t noperands_ = __cs_noperands();
  if (noperands_ < n)
    return ERROR_OPND_STACK_UNDERFLOW;

  size_t base = __cs_operands_base_idx();
  size_t first_off = base + (noperands_ - n /* tail start */);

  // Update noperands
  RETURN_IF_ERROR(__cs_write_imm(__cs_noperands_base_idx(), noperands_ - n));
  if (__cs_sp < n) {
    return ERROR_STACK_UNDERFLOW;
  }
  __cs_sp -= n;
  __gc_sync();

  *ret = csval_intern(first_off);
  return ERROR_NONE;
}

/* Reference */
static error_code_e callstack_get_local_addr(uint32_t index, csval_t *ret) {
  uint32_t nlocals_ = __cs_nlocs();
  if (index >= nlocals_) {
    return ERROR_LOCL_IDX_OUT_OF_RANGE;
  }

  size_t slot_off = __cs_locs_base_idx() + index;
  *ret = csval_intern(slot_off);
  return ERROR_NONE;
}
static error_code_e callstack_get_arg_addr(uint32_t index, csval_t *ret) {
  uint32_t nargs_ = cs_nargs();
  if (index >= nargs_) {
    return ERROR_ARG_IDX_OUT_OF_RANGE;
  }

  size_t slot_off = __cs_args_base_idx() + index;
  *ret = csval_intern(slot_off);
  return ERROR_NONE;
}
static error_code_e callstack_get_glob_addr(uint32_t index, csval_t *ret) {
  uint32_t nglob_ = __cs_nglob;
  if (index >= nglob_) {
    return ERROR_GLOB_IDX_OUT_OF_RANGE;
  }

  size_t slot_off = __cs_globs_base_idx() + index;
  *ret = csval_intern(slot_off);
  return ERROR_NONE;
}

static error_code_e csval_to_ref_aintp(csval_t val, aint **ret) {
  switch (val.ty) {
  case CS_EXTERNAL_REF:
    *ret = (aint *)val.val;
    return ERROR_NONE;
  case CS_INTERNAL_REF: {
    size_t slot = (size_t)UNBOX(val.val);
    if (slot >= __cs_cap) {
      return ERROR_STACK_UNDERFLOW;
    }
    *ret = &__cs_ram_layout[__csval_slot_to_ram_idx(slot)];
    return ERROR_NONE;
  }
  case CS_IMM:
  default:
    return ERROR_NOT_REF;
  }
}

#endif