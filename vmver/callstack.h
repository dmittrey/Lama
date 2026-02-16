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

#define CALLSTACK_INITIAL_SIZE 65536
#define CALLSTACK_MAX_SIZE 262144

#define CSVAL_WORDS 2
#define CS_SLOT_BYTES ((size_t)(CSVAL_WORDS * sizeof(aint)))

/*
| closure | ret_off   | nargs     | prev_fp | - HEADER
| nlocals | locals(N) | noperands |           - DATA
*/
#define CFRAME_HEADER_SZ (4 * CS_SLOT_BYTES)
#define CFRAME_DATA_SZ(NLOCALS) ((1 + NLOCALS + 1) * CS_SLOT_BYTES)

/*
Call frame memory layout(RAM):
+----------------------------------+ <- __gc_stack_top
| globals[0..nglobals-1] (csval_t) |
+----------------------------------+
| ...                              |
+----------------------------------+
| arguments (csval_t)              |
+----------------------------------+ <- (__gc_stack_bottom when push new frame)
| closure | BOX(0)                 | closure ptr or BOX(0)
+----------------------------------+
| ret_off (csval_t)                |
+----------------------------------+
| nargs (csval_t)                  |
+----------------------------------+
| prev_fp (csval_t)                |   // 0 => no prev frame
+----------------------------------+ <- FP
| max depth (csval_t)              |
+----------------------------------+
| nlocals (csval_t)                |
+----------------------------------+
| locals[0..nlocals-1] (csval_t)   |   // VM values
+----------------------------------+
| noperands (csval_t)              |
+----------------------------------+
| operands  (csval_t[])            |   // VM values
+----------------------------------+ <- __gc_stack_bottom
| free                             |
+----------------------------------+
||
|| grow down
\/
*/

extern size_t __gc_stack_top, __gc_stack_bottom;

/* Virtual regs */
extern size_t __cs_fp; /* slot index of max depth for current frame */

static inline size_t __cs_sp_slots(void) {
  return ((size_t)__gc_stack_bottom - __gc_stack_top) / CS_SLOT_BYTES;
}
static inline void __cs_sp_set(size_t slots) {
  __gc_stack_bottom = __gc_stack_top + slots * CS_SLOT_BYTES;
}
static inline void __cs_sp_add(size_t n) {
  __gc_stack_bottom += n * CS_SLOT_BYTES;
}
static inline void __cs_sp_sub(size_t n) {
  __gc_stack_bottom -= n * CS_SLOT_BYTES;
}

/* Internal */
extern size_t __cs_cap;     /* allocated capacity (csval slots) */
extern size_t __cs_nframes; /* prevent underflow */
extern size_t __cs_nglob;   /* number of globals */

/* Interact with RAM layout */
static inline size_t __csval_slot_to_ram_idx(size_t slot) {
  return slot * (size_t)CSVAL_WORDS;
}
static inline void __cs_slot_write(size_t slot, csval_t v) {
  size_t w = __csval_slot_to_ram_idx(slot);
  aint *ram = (aint *)__gc_stack_top;
  ram[w] = BOX((aint)v.ty); /* to skip during GC scan */
  ram[w + 1] = v.val;
}
static inline csval_t __cs_slot_read(size_t slot) {
  size_t w = __csval_slot_to_ram_idx(slot);
  aint *ram = (aint *)__gc_stack_top;
  aint type_word = ram[w];
  return (csval_t){.ty = (csval_type_e)UNBOX(type_word), .val = ram[w + 1]};
}

/* Link with csval interface */
static inline void __cs_write_imm(size_t slot, uint32_t value) {
  __cs_slot_write(slot, csval_imm(value));
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

  size_t used_bytes = (size_t)__gc_stack_bottom - __gc_stack_top;
  aint *new_layout =
      (aint *)realloc((void *)__gc_stack_top, new_cap * CS_SLOT_BYTES);
  if (!new_layout) {
    fprintf(stderr, "Not enough memory to realloc callstack with size %lu\n",
            (unsigned long)new_cap);
    free((void *)__gc_stack_top);
    return ERROR_NOT_ENOUGH_MEMORY;
  }

  __gc_stack_top = (size_t)new_layout;
  __gc_stack_bottom = __gc_stack_top + used_bytes;
  __cs_cap = new_cap;
  return ERROR_NONE;
}
static inline error_code_e __cs_push_slot(csval_t v) {
  RETURN_IF_ERROR(__cs_ensure_capacity(__cs_sp_slots() + 1));
  __cs_slot_write(__cs_sp_slots(), v);
  __cs_sp_add(1);
  return ERROR_NONE;
}
static inline error_code_e __cs_pop_slot(csval_t *ret) {
  __cs_sp_sub(1);
  if (!ret)
    return ERROR_NONE; // To not stubbing ret container
  *ret = __cs_slot_read(__cs_sp_slots());
  return ERROR_NONE;
}

/* Segments (csval slots) */
static inline size_t __cs_globs_base_idx() { return 0; } /* layout start */
static inline size_t __cs_max_depth_base_idx() { return __cs_fp; }
static inline size_t cs_max_depth() {
  uint32_t mdepth;
  if (!__cs_nframes) // First frame TODO erase?
    return 0;
  if (__cs_read_imm_slot(__cs_max_depth_base_idx(), &mdepth))
    return 0;
  return mdepth;
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
static inline size_t __cs_nlocs_base_idx() {
  return __cs_max_depth_base_idx() + 1;
}
static inline size_t __cs_nlocs() {
  uint32_t nlocs;
  if (!__cs_nframes) // First frame TODO erase?
    return 0;
  if (__cs_read_imm_slot(__cs_nlocs_base_idx(), &nlocs))
    return 0;
  return nlocs;
}
static inline size_t __cs_locs_base_idx() { return __cs_nlocs_base_idx() + 1; }
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
/* Frame operations */
static error_code_e cs_push_cframe(aint closure, uint32_t ret_off,
                                   uint32_t nargs) {
  if (!((UNBOXED(closure) && UNBOX(closure) == 0) || !UNBOXED(closure)))
    return ERROR_NOT_VALID_CLOSURE;

  /* Closure segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_from_aint(closure)));

  /* Return address segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(ret_off)));

  /* Args segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(nargs)));

  /* Prolog */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(__cs_fp)));
  __cs_fp = __cs_sp_slots();
  __cs_nframes++;
  return ERROR_NONE;
}
static error_code_e cs_push_frame(uint32_t ret_off, uint32_t nargs) {
  return cs_push_cframe(BOX(0), ret_off, nargs);
}
/* Lifecycle */
static error_code_e cs_reset() {
  /* Virt regs */
  __cs_fp = (size_t)__cs_nglob;
  __cs_sp_set(__cs_fp); /* start callstack from global area end */

  /* Internal */
  __cs_nframes = 0;

  /* Initial frame */
  RETURN_IF_ERROR(cs_push_frame(0, 2));

  return ERROR_NONE;
}
static error_code_e cs_init(int nglobals, size_t initial_size) {
  __gc_init();

  /* RAM layout */
  void *ram = malloc(initial_size);
  if (!ram) {
    fprintf(stderr, "callstack: unable to allocate size=%lu\n", initial_size);
    return ERROR_NOT_ENOUGH_MEMORY;
  }
  __gc_stack_top = (size_t)ram;

  /* Virt regs */
  __cs_fp = (size_t)nglobals;
  __cs_sp_set(__cs_fp); /* start callstack from global area end */

  /* Internal */
  __cs_cap = initial_size / CS_SLOT_BYTES;
  __cs_nframes = 0;
  __cs_nglob = nglobals;

  /* Initial frame */
  RETURN_IF_ERROR(cs_push_frame(0, 2));

  return ERROR_NONE;
}
static error_code_e cs_shutdown(void) {
  free((void *)__gc_stack_top);
  return ERROR_NONE;
}
static error_code_e cs_alloc_locals(uint16_t nlocals, uint16_t max_depth) {
  /* Depth segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(max_depth))); // max depth

  /* Locals segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(nlocals)));
  __cs_sp_add(nlocals); /* reserve space in frame for nlocals */

  /* Operands segment */
  RETURN_IF_ERROR(__cs_push_slot(csval_imm(0))); // noperands
  return ERROR_NONE;
}
static error_code_e cs_pop_frame(uint32_t *ret_off) {
  /* Epilog */
  __cs_sp_set(__cs_fp);
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
  __cs_slot_write(base + index, value);
  return ERROR_NONE;
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
  __cs_slot_write(base + index, value);
  return ERROR_NONE;
}
static csval_t callstack_get_glob(uint32_t index) {
  size_t base = __cs_globs_base_idx();
  return __cs_slot_read(base + index);
}
static void callstack_set_glob(uint32_t index, csval_t value) {
  size_t base = __cs_globs_base_idx();
  __cs_slot_write(base + index, value);
}

/* Operands stack */
static error_code_e callstack_pop_operand(csval_t *ret) {
  uint32_t noperands_ = __cs_noperands();
  RETURN_IF_ERROR(__cs_pop_slot(ret));
  __cs_write_imm(__cs_noperands_base_idx(), noperands_ - 1);
  return ERROR_NONE;
}
static error_code_e callstack_push_operand(csval_t value) {
  uint32_t noperands_ = __cs_noperands();
  RETURN_IF_ERROR(__cs_push_slot(value));
  __cs_write_imm(__cs_noperands_base_idx(), noperands_ + 1);
  return ERROR_NONE;
}
static void callstack_pop_n_operands(uint32_t n) {
  uint32_t noperands_ = __cs_noperands();

  // Update noperands
  __cs_write_imm(__cs_noperands_base_idx(), noperands_ - n);

  __cs_sp_sub(n);
}
static csval_t callstack_operands_tail_ref(uint32_t n) {
  size_t base = __cs_operands_base_idx();
  size_t slot = base + (__cs_noperands() - n /* tail start */);

  return csval_intern(slot);
}

/* Reference */
static error_code_e callstack_get_local_addr(uint32_t index, csval_t *ret) {
  uint32_t nlocals_ = __cs_nlocs();
  if (index >= nlocals_)
    return ERROR_LOCL_IDX_OUT_OF_RANGE;

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
    *ret = &((aint *)__gc_stack_top)[__csval_slot_to_ram_idx(slot)];
    return ERROR_NONE;
  }
  case CS_IMM:
  default:
    return ERROR_NOT_REF;
  }
}

#endif