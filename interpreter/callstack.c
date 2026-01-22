#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "callstack.h"

#define CALLSTACK_INITIAL_SIZE 64
#define CALLSTACK_MAX_SIZE 1024

#define CS_ASSERT_UNBOXED(memo, x)                                             \
  do {                                                                         \
    if (!UNBOXED(x)) {                                                         \
      fprintf(stderr, "unboxed value expected in %s\n", memo);                 \
      abort();                                                                 \
    }                                                                          \
  } while (0)

#define CS_ASSERT_BOXED(memo, x)                                               \
  do {                                                                         \
    if (UNBOXED(x)) {                                                          \
      fprintf(stderr, "boxed value expected in %s\n", memo);                   \
      abort();                                                                 \
    }                                                                          \
  } while (0)

/*
Call frame memory layout(RAM):
+-----------------------------+
| arguments (aint)            |
+-----------------------------+ <- (SP when push new frame)
| BOX(ret_off) (aint)         |
+-----------------------------+
| BOX(narguments) (aint)      |
+-----------------------------+
| BOX(prev_fp)                |   // 0 => no prev frame
+-----------------------------+ <- FP
| BOX(nlocals) (aint)         |
+--------------------------------+
| locals[0..nlocals-1] (aint[])  |   // VM values (boxed heap ptr or imm)
+--------------------------------+
| BOX(noperands) (aint)       |
+-----------------------------+
| operands  (aint[])          |   // VM values (boxed heap ptr or imm)
+-----------------------------+ <- SP
| free                        |
+-----------------------------+
||
|| grow down
\/
*/

typedef struct callstack_t {
  /* Virtual regs */
  size_t fp; /* base of BOX(nlocals) for current frame */
  size_t sp; /* index of next free word */

  /* Internal */
  size_t capacity; /* allocated capacity (aint slots) */

  /* Prevent underflow */
  // TODO Переделать на секцию активации на дне стека чтобы при обращении можно
  // было почистить и вернуть в pool
  size_t nframes;

  /* RAM layout */
  aint *ram_layout; /* aint slots */
} callstack_t;

/* Helpers */
static inline void realloc_if_need(callstack_t *stack) {
  if (stack->sp <= stack->capacity) {
    return;
  }

  size_t new_cap = stack->capacity * 2;
  aint *new_layout = (aint *)realloc(stack->ram_layout, new_cap * sizeof(aint));
  if (new_layout == NULL) {
    fprintf(stderr, "Not enough memory to realloc callstack with size %lu\n",
            (unsigned long)new_cap);
    free(stack->ram_layout);
    exit(1);
  }

  stack->ram_layout = new_layout;
  stack->capacity = new_cap;
}

// Push
static inline void cs_push_aint(callstack_t *s, aint v) {
  s->sp++;
  realloc_if_need(s);
  s->ram_layout[s->sp - 1] = v;
}

#define PUSHIMM(S, IMM) cs_push_aint(S, BOX((aint)IMM))

// POP
static inline aint cs_pop_aint(callstack_t *s) {
  aint v;
  assert(s->sp > 0);
  return s->ram_layout[--s->sp];
}

static inline aint cs_pop_imm(callstack_t *s) {
  aint imm = cs_pop_aint(s);
  CS_ASSERT_UNBOXED("cs_pop_imm", imm);
  return UNBOX(imm);
}

#define POP(S) cs_pop_aint(S)
#define POPIMM(S) cs_pop_imm(S)

/* Segment base idx (relative to fp) */
static inline size_t nlocals_base_idx(callstack_t *s) { return s->fp; }
static inline uint32_t nlocals(callstack_t *s) {
  aint v = s->ram_layout[nlocals_base_idx(s)];
  CS_ASSERT_UNBOXED("nlocals", v);
  return (uint32_t)UNBOX(v);
}

static inline size_t prev_fp_base_idx(callstack_t *s) { return s->fp - 1; }
static inline size_t nargs_base_idx(callstack_t *s) { return s->fp - 2; }
static inline uint32_t nargs(callstack_t *s) {
  aint v = s->ram_layout[nargs_base_idx(s)];
  CS_ASSERT_UNBOXED("nargs", v);
  return (uint32_t)UNBOX(v);
}

static inline size_t ret_off_base_idx(callstack_t *s) { return s->fp - 3; }
static inline size_t args_base_idx(callstack_t *s) {
  return ret_off_base_idx(s) - (size_t)nargs(s);
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
  aint v = s->ram_layout[noperands_base_idx(s)];
  CS_ASSERT_UNBOXED("noperands", v);
  return (uint32_t)UNBOX(v);
}

/* Public Helpers */
uint32_t callstack_nargs(callstack_t *s) { return nargs(s); }
size_t callstack_nframes(callstack_t *s) { return s->nframes; }

/* Lifecycle */
callstack_t *create_callstack() {
  callstack_t *stack = malloc(sizeof(callstack_t));
  if (!stack) {
    return NULL;
  }

  /* RAM layout */
  stack->ram_layout = malloc((size_t)CALLSTACK_INITIAL_SIZE * sizeof(aint));
  if (!stack->ram_layout) {
    free(stack);
    return NULL;
  }

  /* Virt regs */
  stack->fp = 0;
  stack->sp = 0;

  /* Internal */
  stack->capacity = (size_t)CALLSTACK_INITIAL_SIZE;
  stack->nframes = 0;

  return stack;
}

void destroy_callstack(callstack_t *stack) {
  if (stack) {
    free(stack->ram_layout);
    free(stack);
  }
}

/* Frame operations */
void callstack_push_frame(callstack_t *stack, uint32_t ret_off,
                          uint32_t nargument) {
  /* Return address segment */
  PUSHIMM(stack, ret_off); // Return offset

  /* Args segment */
  PUSHIMM(stack, nargument);

  /* Prolog */
  PUSHIMM(stack, stack->fp);
  stack->fp = stack->sp;
  stack->nframes++;
}
void callstack_alloc_locals(callstack_t *stack, uint32_t nlocals) {
  assert(stack->fp == stack->sp);

  /* Locals segment */
  PUSHIMM(stack, nlocals);
  // Error avoid way(for me:)) to reserve space in frame for nlocals
  for (size_t i = 0; i < nlocals; i++)
    PUSHIMM(stack, 0);

  /* Operands segment */
  PUSHIMM(stack, 0); // noperands
}
uint32_t callstack_pop_frame(callstack_t *stack) {
  assert(stack->nframes > 0);

  /* Epilog */
  stack->sp = stack->fp;
  stack->fp = (size_t)POPIMM(stack);
  stack->nframes--;

  /* Args segment */
  POPIMM(stack);

  /* Return address segment */
  return POPIMM(stack);
}

/* Access arguments and locals */
aint callstack_get_local(callstack_t *stack, uint32_t index) {
  uint32_t nlocals_ = nlocals(stack);
  if (index >= nlocals_) {
    fprintf(stderr, "Invalid local access: index %u, nlocals %u\n", index,
            nlocals_);
    exit(1);
  }

  size_t base = locals_base_idx(stack);
  return stack->ram_layout[base + index];
}

void callstack_set_local(callstack_t *stack, uint32_t index, aint value) {

  uint32_t nlocals_ = nlocals(stack);
  if (index >= nlocals_) {
    fprintf(stderr, "Invalid local access: index %u, nlocals %u\n", index,
            nlocals_);
    exit(1);
  }

  size_t base = locals_base_idx(stack);
  stack->ram_layout[base + index] = value;
}
aint callstack_get_arg(callstack_t *stack, uint32_t index) {
  uint32_t nargs_ = nargs(stack);
  if (index >= nargs_) {
    fprintf(stderr, "Invalid argument access: index %u, nargs %u\n", index,
            nargs_);
    exit(1);
  }

  size_t base = args_base_idx(stack);
  return stack->ram_layout[base + index];
}
void callstack_set_arg(callstack_t *stack, uint32_t index, aint value) {
  uint32_t nargs_ = nargs(stack);
  if (index >= nargs_) {
    fprintf(stderr, "Invalid argument access: index %u, nargs %u\n", index,
            nargs_);
    exit(1);
  }

  size_t base = args_base_idx(stack);
  stack->ram_layout[base + index] = value;
}

/* Operands stack */
aint callstack_pop_operand(callstack_t *stack) {
  uint32_t noperands_ = noperands(stack);

  if (noperands_ == 0) {
    fprintf(stderr, "Operands stack underflow\n");
    exit(1);
  }

  size_t base = noperands_base_idx(stack);
  stack->ram_layout[base] = BOX(noperands_ - 1);

  return POP(stack); // Free boxed val?
}
void callstack_push_operand(callstack_t *stack, aint value) {
  uint32_t noperands_ = noperands(stack);

  size_t base = noperands_base_idx(stack);
  stack->ram_layout[base] = BOX(noperands_ + 1);

  PUSH(stack, value);
}
