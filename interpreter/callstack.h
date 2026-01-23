#ifndef CALLSTACK_H
#define CALLSTACK_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_common.h"

/* Opaque */
struct callstack_t;

/* Helpers */
uint32_t callstack_nlocals(struct callstack_t *s);
uint32_t callstack_nargs(struct callstack_t *s);
size_t callstack_nframes(struct callstack_t *s);

/* Reference */
aint *callstack_local_addr(struct callstack_t *s, uint32_t index);
aint *callstack_arg_addr(struct callstack_t *s, uint32_t index);
aint *callstack_resolve_ref(struct callstack_t *s, aint ref);

/* Lifecycle */
struct callstack_t *create_callstack();
void destroy_callstack(struct callstack_t *);

/* Frame operations */
void callstack_push_frame(struct callstack_t *stack, uint32_t ret_off,
                          uint32_t nargs);
void callstack_alloc_locals(struct callstack_t *stack, uint32_t nlocals);
uint32_t callstack_pop_frame(struct callstack_t *stack);

/* Access arguments and locals */
aint callstack_get_local(struct callstack_t *stack, uint32_t index);
void callstack_set_local(struct callstack_t *stack, uint32_t index, aint value);
aint callstack_get_arg(struct callstack_t *stack, uint32_t index);
void callstack_set_arg(struct callstack_t *stack, uint32_t index, aint value);

/* Operands stack */
aint callstack_pop_operand(struct callstack_t *stack);
void callstack_pop_n_operands(struct callstack_t *stack, uint32_t n);
void callstack_push_operand(struct callstack_t *stack, aint value);
aint *callstack_n_last_operands_sequence(struct callstack_t *stack, uint32_t n);

#endif