#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "callstack.h"
#include "gc.h"

#define CALLSTACK_INITIAL_SIZE 64
#define CALLSTACK_MAX_SIZE 32768

extern size_t __gc_stack_top, __gc_stack_bottom;

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

typedef struct callstack_t {
  /* Virtual regs */
  size_t fp; /* slot index of nlocals for current frame */
  size_t sp; /* slot index of next free entry */

  /* Internal */
  size_t capacity; /* allocated capacity (csval slots) */
  size_t nframes;  /* prevent underflow */

  /* Globals */
  size_t nglobals; /* number of globals */

  /* RAM layout */
  aint *ram_layout;
} callstack_t;

/* Helpers */
static inline void gc_sync(callstack_t *stack) {
  __gc_stack_top = (size_t)stack->ram_layout;
  __gc_stack_bottom =
      (size_t)(stack->ram_layout + (stack->sp * (size_t)CSVAL_WORDS));
}

/* Amount of vals to size of this area in stack */
static inline size_t nvals_to_size(size_t nwords) {
  return nwords * (size_t)CSVAL_WORDS * sizeof(aint);
}

/* Number of val slot to stack idx */
static inline size_t slot_word_index(size_t slot) {
  return slot * (size_t)CSVAL_WORDS;
}

static inline error_code_e ensure_capacity(callstack_t *stack,
                                           size_t required_slots) {
  if (required_slots <= stack->capacity) {
    return ERROR_NONE;
  }
  if (required_slots > CALLSTACK_MAX_SIZE) {
    return ERROR_STACK_OVERFLOW;
  }

  size_t new_cap = stack->capacity;
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
      stack->ram_layout, new_cap * (size_t)CSVAL_WORDS * sizeof(aint));
  if (!new_layout) {
    fprintf(stderr, "Not enough memory to realloc callstack with size %lu\n",
            (unsigned long)new_cap);
    free(stack->ram_layout);
    return ERROR_NOT_ENOUGH_MEMORY;
  }

  stack->ram_layout = new_layout;
  stack->capacity = new_cap;
  gc_sync(stack);
  return ERROR_NONE;
}

// No way to create internal ref outside
static inline csval_t csval_from_aint(aint v) {
  return UNBOXED(v) ? csval_imm(v) : csval_extern(v);
}

static inline error_code_e slot_write(callstack_t *s, size_t slot, csval_t v) {
  size_t w = slot_word_index(slot);
  s->ram_layout[w] = BOX((aint)v.ty);
  s->ram_layout[w + 1] = v.val;
  return ERROR_NONE;
}

static inline csval_t slot_read(callstack_t *s, size_t slot) {
  size_t w = slot_word_index(slot);
  aint type_word = s->ram_layout[w];
  assert(UNBOXED(type_word));
  return (csval_t){.ty = (csval_type_e)UNBOX(type_word),
                   .val = s->ram_layout[w + 1]};
}

static inline error_code_e push_slot(callstack_t *s, csval_t v) {
  RETURN_IF_ERROR(ensure_capacity(s, s->sp + 1));
  RETURN_IF_ERROR(slot_write(s, s->sp, v));
  s->sp++;
  gc_sync(s);
  return ERROR_NONE;
}

static inline error_code_e pop_slot(callstack_t *s, csval_t *ret) {
  if (s->sp == 0) {
    return ERROR_OPND_STACK_UNDERFLOW;
  }
  s->sp--;
  *ret = slot_read(s, s->sp);
  gc_sync(s);
  return ERROR_NONE;
}

static inline error_code_e pop_imm(callstack_t *s, uint32_t *out) {
  csval_t v;
  RETURN_IF_ERROR(pop_slot(s, &v));
  if (v.ty != CS_IMM || !UNBOXED(v.val)) {
    return ERROR_NOT_UNBOXED;
  }
  *out = (uint32_t)UNBOX(v.val);
  return ERROR_NONE;
}

static inline error_code_e read_imm_slot(callstack_t *s, size_t slot,
                                         uint32_t *out) {
  csval_t v = slot_read(s, slot);
  if (v.ty != CS_IMM) {
    return ERROR_NOT_UNBOXED;
  }
  if (!UNBOXED(v.val)) {
    return ERROR_NOT_UNBOXED;
  }
  *out = (uint32_t)UNBOX(v.val);
  return ERROR_NONE;
}

static inline error_code_e write_imm_slot(callstack_t *s, size_t slot,
                                          uint32_t value) {
  return slot_write(s, slot, csval_imm(BOX((aint)value)));
}

/* Segment base idx (relative to fp) */
static inline size_t globals_base_idx(callstack_t *s) { /* layout start */
  return 0;
}
static inline size_t nlocals_base_idx(callstack_t *s) { return s->fp; }
static inline uint32_t nlocals(callstack_t *s) {
  if (s->nframes == 0) {
    return 0;
  }
  uint32_t v = 0;
  if (read_imm_slot(s, nlocals_base_idx(s), &v) != ERROR_NONE) {
    return 0;
  }
  return v;
}

static inline size_t prev_fp_base_idx(callstack_t *s) { return s->fp - 1; }
static inline size_t nargs_base_idx(callstack_t *s) { return s->fp - 2; }
static inline uint32_t nargs(callstack_t *s) {
  if (s->nframes == 0) {
    return 0;
  }
  uint32_t v = 0;
  if (read_imm_slot(s, nargs_base_idx(s), &v) != ERROR_NONE) {
    return 0;
  }
  return v;
}

static inline size_t ret_off_base_idx(callstack_t *s) { return s->fp - 3; }
static inline size_t closure_base_idx(callstack_t *s) { return s->fp - 4; }
static inline aint closure(callstack_t *s) {
  if (s->nframes == 0) {
    return BOX(0);
  }
  csval_t v = slot_read(s, closure_base_idx(s));
  return v.val;
}
static inline size_t args_base_idx(callstack_t *s) {
  return closure_base_idx(s) - (size_t)nargs(s);
}

static inline size_t locals_base_idx(callstack_t *s) { return s->fp + 1; }
static inline size_t noperands_base_idx(callstack_t *s) {
  return locals_base_idx(s) + (size_t)nlocals(s);
}
static inline size_t operands_base_idx(callstack_t *s) {
  return noperands_base_idx(s) + 1;
}

/* noperands etc. */
static inline uint32_t noperands(callstack_t *s) {
  if (s->nframes == 0) {
    return 0;
  }
  uint32_t v = 0;
  if (read_imm_slot(s, noperands_base_idx(s), &v) != ERROR_NONE) {
    return 0;
  }
  return v;
}

/* Public Helpers */
uint32_t callstack_nlocals(callstack_t *s) { return nlocals(s); }
uint32_t callstack_noperands(callstack_t *s) { return noperands(s); }
uint32_t callstack_nargs(callstack_t *s) { return nargs(s); }
size_t callstack_nframes(callstack_t *s) { return s->nframes; }
size_t callstack_nglobals(callstack_t *s) { return s->nglobals; }
aint callstack_closure(callstack_t *s) { return closure(s); }

/* Lifecycle */
callstack_t *create_callstack(int nglobals) {
  __gc_init();

  callstack_t *stack = malloc(sizeof(callstack_t));
  if (!stack) {
    return NULL;
  }

  /* RAM layout */
  stack->ram_layout = malloc(nvals_to_size(CALLSTACK_INITIAL_SIZE));
  if (!stack->ram_layout) {
    fprintf(stderr, "callstack: unable to allocate size=%lu\n",
            nvals_to_size(CALLSTACK_INITIAL_SIZE));
    free(stack);
    return NULL;
  }

  /* Globals */
  stack->nglobals = nglobals;
  size_t glob_area_sz = nvals_to_size(nglobals);

  /* Virt regs */
  stack->fp = glob_area_sz;
  stack->sp = glob_area_sz; // Start callstack from global area

  /* Internal */
  stack->capacity = (size_t)CALLSTACK_INITIAL_SIZE;
  stack->nframes = 0;

  gc_sync(stack);
  return stack;
}

void destroy_callstack(callstack_t *stack) {
  if (stack) {
    free(stack->ram_layout);
    free(stack);
  }
}

/* Frame operations */
error_code_e callstack_push_cframe(callstack_t *stack, aint closure,
                                   uint32_t ret_off, uint32_t nargument) {
  if (!((UNBOXED(closure) && UNBOX(closure) == 0) || !UNBOXED(closure)))
    return ERROR_NOT_VALID_CLOSURE;

  if (nargument > 0) {
    uint32_t noperands_ = noperands(stack);
    if (nargument > noperands_) {
      return ERROR_OPND_STACK_UNDERFLOW;
    }
    /* Detach args from operand stack without moving slots. */
    RETURN_IF_ERROR(write_imm_slot(stack, noperands_base_idx(stack),
                                   noperands_ - nargument));
  }

  /* Closure segment */
  RETURN_IF_ERROR(push_slot(stack, csval_from_aint(closure)));

  /* Return address segment */
  RETURN_IF_ERROR(push_slot(stack, csval_imm(BOX((aint)ret_off))));

  /* Args segment */
  RETURN_IF_ERROR(push_slot(stack, csval_imm(BOX((aint)nargument))));

  /* Prolog */
  RETURN_IF_ERROR(push_slot(stack, csval_imm(BOX((aint)stack->fp))));
  stack->fp = stack->sp;
  stack->nframes++;
  return ERROR_NONE;
}
error_code_e callstack_push_frame(callstack_t *stack, uint32_t ret_off,
                                  uint32_t nargument) {
  return callstack_push_cframe(stack, BOX(0), ret_off, nargument);
}
error_code_e callstack_alloc_locals(callstack_t *stack, uint32_t nlocals) {
  if (stack->nframes == 0) {
    RETURN_IF_ERROR(callstack_push_frame(stack, 0, 0));
  }

  /* Locals segment */
  RETURN_IF_ERROR(push_slot(stack, csval_imm(BOX((aint)nlocals))));
  // Reserve space in frame for nlocals
  for (size_t i = 0; i < nlocals; i++)
    RETURN_IF_ERROR(push_slot(stack, csval_imm(BOX(0))));

  /* Operands segment */
  RETURN_IF_ERROR(push_slot(stack, csval_imm(BOX(0)))); // noperands
  return ERROR_NONE;
}
error_code_e callstack_pop_frame(callstack_t *stack, uint32_t *ret_off) {
  if (stack->nframes == 0)
    return ERROR_STACK_UNDERFLOW;

  /* Epilog */
  stack->sp = stack->fp;
  uint32_t prev_fp = 0;
  RETURN_IF_ERROR(pop_imm(stack, &prev_fp));
  stack->fp = (size_t)prev_fp;
  stack->nframes--;

  /* Args segment */
  uint32_t nargs_ = 0;
  RETURN_IF_ERROR(pop_imm(stack, &nargs_));

  /* Return address segment */
  RETURN_IF_ERROR(pop_imm(stack, ret_off));

  /* Closure segment */
  csval_t closure_val;
  RETURN_IF_ERROR(pop_slot(stack, &closure_val));

  if (stack->sp < (size_t)nargs_) {
    return ERROR_STACK_UNDERFLOW;
  }
  stack->sp -= (size_t)nargs_;

  gc_sync(stack);
  return ERROR_NONE;
}

/* Access arguments and locals */
error_code_e callstack_get_local(callstack_t *stack, uint32_t index,
                                 csval_t *ret) {
  uint32_t nlocals_ = nlocals(stack);
  if (index >= nlocals_)
    return ERROR_LOCL_IDX_OUT_OF_RANGE;

  size_t base = locals_base_idx(stack);
  *ret = slot_read(stack, base + index);
  return ERROR_NONE;
}
error_code_e callstack_set_local(callstack_t *stack, uint32_t index,
                                 csval_t value) {
  uint32_t nlocals_ = nlocals(stack);
  if (index >= nlocals_)
    return ERROR_LOCL_IDX_OUT_OF_RANGE;

  size_t base = locals_base_idx(stack);
  return slot_write(stack, base + index, value);
}

error_code_e callstack_get_arg(callstack_t *stack, uint32_t index,
                               csval_t *ret) {
  uint32_t nargs_ = nargs(stack);
  if (index >= nargs_)
    return ERROR_ARG_IDX_OUT_OF_RANGE;

  size_t base = args_base_idx(stack);
  *ret = slot_read(stack, base + index);
  return ERROR_NONE;
}
error_code_e callstack_set_arg(callstack_t *stack, uint32_t index,
                               csval_t value) {
  uint32_t nargs_ = nargs(stack);
  if (index >= nargs_)
    return ERROR_ARG_IDX_OUT_OF_RANGE;

  size_t base = args_base_idx(stack);
  return slot_write(stack, base + index, value);
}
error_code_e callstack_get_glob(struct callstack_t *stack, uint32_t index,
                                csval_t *ret) {
  uint32_t nglobals_ = stack->nglobals;
  if (index >= nglobals_)
    return ERROR_GLOB_IDX_OUT_OF_RANGE;

  size_t base = globals_base_idx(stack);
  *ret = slot_read(stack, base + index);
  return ERROR_NONE;
}
error_code_e callstack_set_glob(struct callstack_t *stack, uint32_t index,
                                csval_t value) {
  uint32_t nglobals_ = stack->nglobals;
  if (index >= nglobals_)
    return ERROR_GLOB_IDX_OUT_OF_RANGE;

  size_t base = globals_base_idx(stack);
  slot_write(stack, base + index, value);
  return ERROR_NONE;
}

error_code_e csval_to_imm(struct callstack_t *stack, csval_t val, aint *ret) {
  if (val.ty != CS_IMM || !UNBOXED(val.val)) {
    return ERROR_NOT_UNBOXED;
  }
  *ret = val.val;
  return ERROR_NONE;
}

error_code_e csval_to_ref(struct callstack_t *stack, csval_t val, aint **ret) {
  switch (val.ty) {
  case CS_EXTERNAL_REF:
    if (UNBOXED(val.val)) {
      return ERROR_NOT_BOXED;
    }
    *ret = (aint *)val.val;
    return ERROR_NONE;
  case CS_INTERNAL_REF: {
    if (!UNBOXED(val.val)) {
      return ERROR_NOT_UNBOXED;
    }
    size_t slot = (size_t)UNBOX(val.val);
    if (slot >= stack->capacity) {
      return ERROR_STACK_UNDERFLOW;
    }
    *ret = &stack->ram_layout[slot_word_index(slot)];
    return ERROR_NONE;
  }
  case CS_IMM:
  default:
    return ERROR_NOT_BOXED;
  }
}

/* Operands stack */
error_code_e callstack_pop_operand(callstack_t *stack, csval_t *ret) {
  uint32_t noperands_ = noperands(stack);
  if (noperands_ == 0)
    return ERROR_OPND_STACK_UNDERFLOW;

  RETURN_IF_ERROR(pop_slot(stack, ret));
  RETURN_IF_ERROR(
      write_imm_slot(stack, noperands_base_idx(stack), noperands_ - 1));
  return ERROR_NONE;
}
error_code_e callstack_push_operand(callstack_t *stack, csval_t value) {
  uint32_t noperands_ = noperands(stack);
  RETURN_IF_ERROR(push_slot(stack, value));
  RETURN_IF_ERROR(
      write_imm_slot(stack, noperands_base_idx(stack), noperands_ + 1));
  return ERROR_NONE;
}
error_code_e callstack_pop_n_operands(struct callstack_t *stack, uint32_t n,
                                      csval_t *ret) {
  uint32_t noperands_ = noperands(stack);
  if (noperands_ < n)
    return ERROR_OPND_STACK_UNDERFLOW;

  size_t base = operands_base_idx(stack);
  size_t first = base + (noperands_ - n /* tail start */);

  // Update noperands
  RETURN_IF_ERROR(
      write_imm_slot(stack, noperands_base_idx(stack), noperands_ - n));
  if (stack->sp < n) {
    return ERROR_STACK_UNDERFLOW;
  }
  stack->sp -= n;
  gc_sync(stack);

  *ret = csval_intern(BOX((aint)first));
  return ERROR_NONE;
}

/* Reference */
error_code_e callstack_get_local_addr(callstack_t *stack, uint32_t index,
                                      csval_t *ret) {
  uint32_t nlocals_ = nlocals(stack);
  if (index >= nlocals_) {
    return ERROR_LOCL_IDX_OUT_OF_RANGE;
  }

  size_t slot = locals_base_idx(stack) + index;
  *ret = csval_intern(BOX((aint)slot));
  return ERROR_NONE;
}
error_code_e callstack_get_arg_addr(callstack_t *stack, uint32_t index,
                                    csval_t *ret) {
  uint32_t nargs_ = nargs(stack);
  if (index >= nargs_) {
    return ERROR_ARG_IDX_OUT_OF_RANGE;
  }

  size_t slot = args_base_idx(stack) + index;
  *ret = csval_intern(BOX((aint)slot));
  return ERROR_NONE;
}
error_code_e callstack_get_glob_addr(callstack_t *stack, uint32_t index,
                                     csval_t *ret) {
  uint32_t nargs_ = stack->nglobals;
  if (index >= nargs_) {
    return ERROR_GLOB_IDX_OUT_OF_RANGE;
  }

  size_t slot = globals_base_idx(stack) + index;
  *ret = csval_intern(BOX((aint)slot));
  return ERROR_NONE;
}
