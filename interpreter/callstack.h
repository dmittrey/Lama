#ifndef CALLSTACK_H
#define CALLSTACK_H

#include <stdint.h>
#include <stddef.h>

/* Opaque */
struct callstack_t;

/* Helpers */
uint32_t callstack_nargs(struct callstack_t *s);
size_t callstack_nframes(struct callstack_t *s);

/* Lifecycle */
struct callstack_t* create_callstack();
void destroy_callstack(struct callstack_t*);

/* Frame operations */
void callstack_push_frame(struct callstack_t *stack, char* return_addr, uint32_t nargs);
void callstack_alloc_locals(struct callstack_t *stack, uint32_t nlocals);
char* callstack_pop_frame(struct callstack_t *stack);

/* Access arguments and locals */
int32_t callstack_get_local(struct callstack_t *stack, uint32_t index);
void callstack_set_local(struct callstack_t *stack, uint32_t index, int32_t value);
int32_t callstack_get_arg(struct callstack_t *stack, uint32_t index);
void callstack_set_arg(struct callstack_t *stack, uint32_t index, int32_t value);

/* Operands stack */
int32_t callstack_pop_operand(struct callstack_t *stack);
void callstack_push_operand(struct callstack_t *stack, int32_t value);

#endif