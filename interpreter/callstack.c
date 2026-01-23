#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "callstack.h"
#include "gc.h"

#define CALLSTACK_INITIAL_SIZE 64
#define CALLSTACK_MAX_SIZE 1024

#define REF_POOL_SIZE 2048

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

typedef ptrt csword_t;

_Static_assert(sizeof(csword_t) == sizeof(aint),
               "callstack slot size must match aint size");

static inline csword_t aint_to_word(aint v) {
  csword_t w;
  memcpy(&w, &v, sizeof(w));
  return w;
}

static inline aint word_to_aint(csword_t w) {
  aint v;
  memcpy(&v, &w, sizeof(v));
  return v;
}

typedef enum ref_kind {
  REF_LOCAL = 1,
  REF_ARG = 2,
} ref_kind_e;

typedef struct refdesc_t {
  uint8_t kind;   /* ref_kind_t */
  uint16_t index; /* local/arg index */
} refdesc_t;

typedef struct callstack_t {
  /* Virtual regs */
  size_t fp; /* base of BOX(nlocals) for current frame */
  size_t sp; /* index of next free word */
  size_t rp; // Ref pointer (next free idx in refpool)

  /* Internal */
  size_t capacity; /* allocated capacity (aint slots) */
  size_t nframes;

  /* RAM layout */
  csword_t *ram_layout;

  /* Reference pool */
  refdesc_t *ref_pool;
} callstack_t;

/* Helpers */
static inline void gc_sync(callstack_t *stack) {
  gc_set_vm_stack_region((void *)stack->ram_layout,
                         (void *)(stack->ram_layout + stack->sp));
}

static inline void realloc_if_need(callstack_t *stack) {
  if (stack->sp <= stack->capacity) {
    return;
  }

  size_t new_cap = stack->capacity * 2;
  csword_t *new_layout =
      (csword_t *)realloc(stack->ram_layout, new_cap * sizeof(csword_t));
  if (new_layout == NULL) {
    fprintf(stderr, "Not enough memory to realloc callstack with size %lu\n",
            (unsigned long)new_cap);
    free(stack->ram_layout);
    exit(1);
  }

  stack->ram_layout = new_layout;
  stack->capacity = new_cap;
  gc_sync(stack);
}

// Push
static inline void push_aint(callstack_t *s, aint v) {
  s->sp++;
  realloc_if_need(s);
  s->ram_layout[s->sp - 1] = aint_to_word(v);
  gc_sync(s);
}

#define PUSH(S, AINT) push_aint(S, AINT)
#define PUSHIMM(S, IMM) push_aint(S, BOX((aint)IMM))

// POP
static inline aint pop_aint(callstack_t *s) {
  assert(s->sp > 0);
  csword_t w = s->ram_layout[--s->sp];
  gc_sync(s);
  return word_to_aint(w);
}

static inline aint pop_imm(callstack_t *s) {
  aint imm = pop_aint(s);
  CS_ASSERT_UNBOXED("pop_imm", imm);
  return UNBOX(imm);
}

#define POP(S) pop_aint(S)
#define POPIMM(S) pop_imm(S)

/* Segment base idx (relative to fp) */
static inline size_t nlocals_base_idx(callstack_t *s) { return s->fp; }
static inline uint32_t nlocals(callstack_t *s) {
  aint v = word_to_aint(s->ram_layout[nlocals_base_idx(s)]);
  CS_ASSERT_UNBOXED("nlocals", v);
  return (uint32_t)UNBOX(v);
}

static inline size_t prev_fp_base_idx(callstack_t *s) { return s->fp - 1; }
static inline size_t nargs_base_idx(callstack_t *s) { return s->fp - 2; }
static inline uint32_t nargs(callstack_t *s) {
  aint v = word_to_aint(s->ram_layout[nargs_base_idx(s)]);
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

/* slot helpers */
static inline aint *local_slot_addr(callstack_t *s, uint32_t index) {
  size_t base = locals_base_idx(s);
  return (aint *)&s->ram_layout[base + index];
}

static inline aint *arg_slot_addr(callstack_t *s, uint32_t index) {
  size_t base = args_base_idx(s);
  return (aint *)&s->ram_layout[base + index];
}

/* noperands etc. */
static inline uint32_t noperands(callstack_t *s) {
  aint v = word_to_aint(s->ram_layout[noperands_base_idx(s)]);
  CS_ASSERT_UNBOXED("noperands", v);
  return (uint32_t)UNBOX(v);
}

/* Public Helpers */
uint32_t callstack_nlocals(callstack_t *s) { return nlocals(s); }
uint32_t callstack_nargs(callstack_t *s) { return nargs(s); }
size_t callstack_nframes(callstack_t *s) { return s->nframes; }

/* Reference */
aint *callstack_local_addr(callstack_t *s, uint32_t index) {
  uint32_t nlocals_ = nlocals(s);
  if (index >= nlocals_) {
    fprintf(stderr, "Invalid local ref: index %u, nlocals %u\n", index,
            nlocals_);
    exit(1);
  }

  if (s->rp >= REF_POOL_SIZE) {
    fprintf(stderr, "ref_pool exhausted (size=%d)\n", REF_POOL_SIZE);
    exit(1);
  }

  refdesc_t *d = &s->ref_pool[s->rp++];
  d->kind = (uint8_t)REF_LOCAL;
  d->index = (uint16_t)index;

  return (aint *)d;
}
aint *callstack_arg_addr(callstack_t *s, uint32_t index) {
  uint32_t nargs_ = nargs(s);
  if (index >= nargs_) {
    fprintf(stderr, "Invalid arg ref: index %u, nargs %u\n", index, nargs_);
    exit(1);
  }

  if (s->rp >= REF_POOL_SIZE) {
    fprintf(stderr, "ref_pool exhausted (size=%d)\n", REF_POOL_SIZE);
    exit(1);
  }

  refdesc_t *d = &s->ref_pool[s->rp++];
  d->kind = (uint8_t)REF_ARG;
  d->index = (uint16_t)index;

  return (aint *)d;
}
aint *callstack_resolve_ref(callstack_t *s, aint ref) {
  if (UNBOXED(ref)) {
    fprintf(stderr, "resolve_ref: boxed ref expected, got unboxed\n");
    exit(1);
  }

  void *p = (void *)ref;

  /* descriptor case: ref points into ref_pool */
  if (p >= (void *)s->ref_pool && p < (void *)(s->ref_pool + REF_POOL_SIZE)) {

    refdesc_t *d = (refdesc_t *)p;

    switch ((ref_kind_e)d->kind) {
    case REF_LOCAL:
      return local_slot_addr(s, (uint32_t)d->index);
    case REF_ARG:
      return arg_slot_addr(s, (uint32_t)d->index);
    default:
      fprintf(stderr, "resolve_ref: bad ref kind %u\n", (unsigned)d->kind);
      exit(1);
    }
  }

  /* direct-address case (globals, or any external aint-cell pointer) */
  return (aint *)p;
}

/* Lifecycle */
callstack_t *create_callstack() {
  __gc_init();

  callstack_t *stack = malloc(sizeof(callstack_t));
  if (!stack) {
    return NULL;
  }

  /* RAM layout */
  stack->ram_layout = malloc((size_t)CALLSTACK_INITIAL_SIZE * sizeof(csword_t));
  if (!stack->ram_layout) {
    free(stack);
    return NULL;
  }

  /* Reference pool */
  stack->ref_pool =
      (refdesc_t *)malloc((size_t)REF_POOL_SIZE * sizeof(refdesc_t));
  if (!stack->ref_pool) {
    free(stack->ram_layout);
    free(stack);
    return NULL;
  }

  /* Virt regs */
  stack->fp = 0;
  stack->sp = 0;
  stack->rp = 0;

  /* Internal */
  stack->capacity = (size_t)CALLSTACK_INITIAL_SIZE;
  stack->nframes = 0;

  gc_sync(stack);
  return stack;
}

void destroy_callstack(callstack_t *stack) {
  if (stack) {
    free(stack->ref_pool);
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
  gc_sync(stack);

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
  return word_to_aint(stack->ram_layout[base + index]);
}
void callstack_set_local(callstack_t *stack, uint32_t index, aint value) {

  uint32_t nlocals_ = nlocals(stack);
  if (index >= nlocals_) {
    fprintf(stderr, "Invalid local access: index %u, nlocals %u\n", index,
            nlocals_);
    exit(1);
  }

  size_t base = locals_base_idx(stack);
  stack->ram_layout[base + index] = aint_to_word(value);
}

aint callstack_get_arg(callstack_t *stack, uint32_t index) {
  uint32_t nargs_ = nargs(stack);
  if (index >= nargs_) {
    fprintf(stderr, "Invalid argument access: index %u, nargs %u\n", index,
            nargs_);
    exit(1);
  }

  size_t base = args_base_idx(stack);
  return word_to_aint(stack->ram_layout[base + index]);
}
void callstack_set_arg(callstack_t *stack, uint32_t index, aint value) {
  uint32_t nargs_ = nargs(stack);
  if (index >= nargs_) {
    fprintf(stderr, "Invalid argument access: index %u, nargs %u\n", index,
            nargs_);
    exit(1);
  }

  size_t base = args_base_idx(stack);
  stack->ram_layout[base + index] = aint_to_word(value);
}

/* Operands stack */
aint callstack_pop_operand(callstack_t *stack) {
  uint32_t noperands_ = noperands(stack);

  if (noperands_ == 0) {
    fprintf(stderr, "Operands stack underflow\n");
    exit(1);
  }

  size_t base = noperands_base_idx(stack);
  stack->ram_layout[base] = aint_to_word(BOX(noperands_ - 1));

  return POP(stack); // Free boxed val?
}
/*
TODO What to do with extra_roots?

When I’m dealing with `Bstring`, `Bsexp`, etc., they allocate duplicates of
parts of aggregate objects in the GC heap, and they also register handlers for
these arguments—placed on the VM stack—as extra roots. I’m concerned that, in
the future, I won’t have enough capacity to register additional extra roots. In
particular, dangling operands (used in a constructor and then popped) end up
below `gc_vm_bottom`, are not indexed as roots, and the GC therefore cannot
remove them from the `extra_roots` collection.
*/

void callstack_pop_n_operands(struct callstack_t *stack, uint32_t n) {
  uint32_t noperands_ = noperands(stack);
  assert(noperands_ >= n);

  // Update noperands
  size_t nops_base = noperands_base_idx(stack);
  stack->ram_layout[nops_base] = aint_to_word(BOX(noperands_ - n));

  // Update sp and gc ptrs
  stack->sp -= n;
  gc_sync(stack);
}
void callstack_push_operand(callstack_t *stack, aint value) {
  uint32_t noperands_ = noperands(stack);

  size_t base = noperands_base_idx(stack);
  stack->ram_layout[base] = aint_to_word(BOX(noperands_ + 1));

  PUSH(stack, value);
}
// aint* -> aint(мы просто указатель в кучу интерпретируем adaptive int64_t)
aint *callstack_n_last_operands_sequence(callstack_t *stack, uint32_t n) {
  uint32_t noperands_ = noperands(stack);
  assert(noperands_ >= n);

  size_t base = operands_base_idx(stack);
  return (aint *)&stack->ram_layout[base + (noperands_ - n /*cnt of tail*/)];
}
