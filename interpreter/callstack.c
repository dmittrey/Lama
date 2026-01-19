#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "callstack.h"

#define CALLSTACK_INITIAL_SIZE 64
#define CALLSTACK_MAX_SIZE     1024

/*
Call frame memory layout(RAM):
+-----------------------------+
| prev_fp_off (u64)           |
+-----------------------------+ <- FP
| nlocals (u32)               |
+-----------------------------+
| locals[0..nlocals-1] (i32)  |
+-----------------------------+
| noperands (u32)             |
+-----------------------------+
| operands stack (i32)        |
+-----------------------------+ <- SP
| free                        |
+-----------------------------+
*/

typedef struct callstack_t {
    /* Virtual regs */
    char* fp;   /* base of locals for current frame */
    char* sp;   /* top */

    /* Internal */
    size_t size;                /* number of active frames */
    size_t capacity;            /* allocated capacity */

    /* RAM layout */
    char* ram_layout;
} callstack_t;

/* Helpers */
static inline void cs_push_u32(callstack_t *s, uint32_t v) {
    memcpy(s->sp, &v, sizeof(v));
    s->sp += sizeof(uint32_t);
}
static inline void cs_push_u64(callstack_t *s, uint64_t v) {
    memcpy(s->sp, &v, sizeof(v));
    s->sp += sizeof(uint64_t);
}
static inline void cs_push_i32(callstack_t *s, int32_t v) {
    memcpy(s->sp, &v, sizeof(v));
    s->sp += sizeof(int32_t);
}

#define PUSH(S, V) \
    _Generic((V), \
        uint32_t: cs_push_u32, \
        uint64_t: cs_push_u64, \
        int32_t:  cs_push_i32  \
    )((S), (V))

static inline uint32_t cs_pop_u32(callstack_t *s) {
    uint32_t v;
    s->sp -= sizeof(uint32_t);
    memcpy(&v, s->sp, sizeof(v));
    return v;
}
static inline uint64_t cs_pop_u64(callstack_t *s) {
    uint64_t v;
    s->sp -= sizeof(uint64_t);
    memcpy(&v, s->sp, sizeof(v));
    return v;
}
static inline int32_t cs_pop_i32(callstack_t *s) {
    int32_t v;
    s->sp -= sizeof(int32_t);
    memcpy(&v, s->sp, sizeof(v));
    return v;
}

#define POP(S, TYPE) \
    _Generic(((TYPE)0), \
        uint32_t: cs_pop_u32, \
        uint64_t: cs_pop_u64, \
        int32_t:  cs_pop_i32  \
    )((S))

static inline ptrdiff_t local_size(uint32_t nlocals) {
    return sizeof(int32_t) * nlocals;
}

/* Segment base */
static inline char* nlocals_base(callstack_t *s)    { return s->fp; }
static inline char* locals_base(callstack_t *s)     { return nlocals_base(s) + sizeof(uint32_t); }
static inline char* noperands_base(callstack_t *s)  { return locals_base(s) + (sizeof(int32_t) * *nlocals_base(s)); }
static inline char* operands_base(callstack_t *s)   { return noperands_base(s) + sizeof(uint32_t); }

/* Lifecycle */
callstack_t* create_callstack()
{
    callstack_t *stack = malloc(sizeof(callstack_t));
    if (!stack) {
        return NULL;
    }

    /* RAM layout */
    stack->ram_layout = malloc(CALLSTACK_INITIAL_SIZE);
    if (!stack->ram_layout) {
        free(stack);
        return NULL;
    }

    /* Virt regs */
    stack->fp = NULL;
    stack->sp = stack->ram_layout;

    /* Internal */
    stack->size = 0;
    stack->capacity = CALLSTACK_INITIAL_SIZE;

    return stack;
}

void destroy_callstack(callstack_t* stack)
{
    if (stack) {
        free(stack->ram_layout);
        free(stack);
    }
}

/* Frame operations */
void callstack_push_frame(struct callstack_t *stack, uint32_t nlocals)
{
    // Prolog
    PUSH(stack, (uint64_t)stack->fp);
    stack->fp = stack->sp;
    stack->sp += local_size(nlocals);
    
    // Update stack size
    stack->size += sizeof(uint32_t) + local_size(nlocals);
}
void callstack_pop_frame(struct callstack_t *stack)
{
    // Epilog
    char* old_sp = stack->sp;
    stack->sp = stack->fp;
    stack->fp = (char*)POP(stack, uint64_t);

    // Update stack size
    stack->size -= (ptrdiff_t)(old_sp - stack->sp);
}


int32_t callstack_get_local(struct callstack_t *stack, uint32_t index)
{
    callstack_t *s = (callstack_t *)stack;
    assert(s);

    uint32_t nlocals = *((uint32_t*)nlocals_base(stack));
    if (index >= nlocals) {
        fprintf(stderr, "Invalid local access: index %u, nlocals %u\n", index, nlocals);
        exit(1);
    }

    int32_t *locals = (int32_t*)locals_base(stack);
    return locals[index];
}

void callstack_set_local(struct callstack_t *stack, uint32_t index, int32_t value)
{
    callstack_t *s = (callstack_t *)stack;
    assert(s);

    uint32_t nlocals = *((uint32_t*)nlocals_base(stack));
    if (index >= nlocals) {
        fprintf(stderr, "Invalid local access: index %u, nlocals %u\n", index, nlocals);
        exit(1);
    }

    int32_t *locals = (int32_t *)locals_base(stack);
    locals[index] = value;
}

// static inline char* noperands_base(callstack_t *s)  { return locals_base(s) + (sizeof(int32_t) * *nlocals_base(s)); }
// static inline char* operands_base(callstack_t *s)   { return noperands_base(s) + sizeof(uint32_t); }

int32_t callstack_pop_operand(struct callstack_t *stack)
{
    uint32_t* noperands = (uint32_t *)noperands_base(stack);
    assert(*noperands >= 0);

    if (*noperands == 0) {
        fprintf(stderr, "Operands stack underflow\n");
        exit(1);
    }

    *noperands -= 1;
    int32_t operand = POP(stack, int32_t);

    // Update stack size
    stack->size -= sizeof(int32_t);

    return operand;
}
void callstack_push_operand(struct callstack_t *stack, int32_t value)
{
    uint32_t* noperands =  (uint32_t *)noperands_base(stack);

    *noperands += 1;
    PUSH(stack, value);

    // Update stack size
    stack->size += sizeof(int32_t);
}
