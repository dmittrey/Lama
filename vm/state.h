#ifndef STATE_H
#define STATE_H

#include <stddef.h>
#include <stdint.h>

#include "../runtime/runtime.h"

#include "bytecode.h"
#include "callstack.h"
#include "error.h"

/* Opaque */
struct interpreter_state_t;

/* Lifecycle */
struct interpreter_state_t *create_interpreter_state(bytefile *bf);
void destroy_interpreter_state(struct interpreter_state_t *state);

/* Operations */
char *state_ip(struct interpreter_state_t *state);
char *state_base_ip(struct interpreter_state_t *state);
uint32_t state_ip_off(struct interpreter_state_t *state);
struct callstack_t *state_cs(struct interpreter_state_t *state);
int state_read_int(struct interpreter_state_t *state);
char state_read_byte(struct interpreter_state_t *state);
char *state_read_string(struct interpreter_state_t *state);

/* Globals */
error_code_e state_get_glob(struct interpreter_state_t *state, uint32_t index,
                            aint *ret_val);
error_code_e state_set_glob(struct interpreter_state_t *state, uint32_t index,
                            aint value);

#endif
