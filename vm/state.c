#include "state.h"
#include "bytecode.h"
#include "callstack.h"

#include <stdio.h>
#include <stdlib.h>

/* Structure internals */
typedef struct interpreter_state_t {
  /* Virtual registers */
  char *ip; /* address of current instruction */

  /* Memory areas */
  aint *globals;      /* global variables array */
  size_t num_globals; /* number of global variables */

  /* Call stack opaque */
  struct callstack_t *callstack;

  /* Bytecode file */
  bytefile *bf; /* loaded bytecode file */
} interpreter_state_t;

/* Lifecycle */
interpreter_state_t *create_interpreter_state(bytefile *bf) {
  interpreter_state_t *state = malloc(sizeof(interpreter_state_t));
  if (!state) {
    fprintf(stderr, "Failed to allocate interpreter state\n");
    return NULL;
  }

  /* Virtual registers */
  state->ip = bf->code_ptr; /* start at beginning of code */

  /* Memory areas */
  state->num_globals = bf->global_area_size;
  if (state->num_globals > 0) {
    state->globals = calloc(bf->global_area_size, sizeof(aint));
    if (!state->globals) {
      fprintf(stderr, "Failed to allocate global variables\n");
      free(state);
      return NULL;
    }
  } else {
    state->globals = NULL;
  }

  /* Create call stack */
  state->callstack = create_callstack();
  if (!state->callstack) {
    fprintf(stderr, "Failed to create call stack\n");
    free(state);
    return NULL;
  }

  /* Bytecode file */
  state->bf = bf;

  return state;
}
void destroy_interpreter_state(interpreter_state_t *state) {
  if (state) {
    if (state->globals) {
      free(state->globals);
    }
    if (state->callstack) {
      destroy_callstack(state->callstack);
    }
    free(state);
  }
}

/* Operations */
char *state_ip(interpreter_state_t *state) { return state->ip; }
char *state_base_ip(struct interpreter_state_t *state) {
  return state->bf->code_ptr;
}
uint32_t state_ip_off(interpreter_state_t *state) {
  return state->ip - state->bf->code_ptr;
}
struct callstack_t *state_cs(struct interpreter_state_t *state) {
  return state->callstack;
}
int state_read_int(interpreter_state_t *state) {
  state->ip += sizeof(int);
  return *(int *)(state->ip - sizeof(int));
}
char state_read_byte(interpreter_state_t *state) {
  return (unsigned char)*state->ip++;
}
char *state_read_string(interpreter_state_t *state) {
  return get_string(state->bf, state_read_int(state));
}

/* Globals */
error_code_e state_get_glob(interpreter_state_t *state, uint32_t index,
                            aint *ret_val) {
  if (index < 0)
    return ERROR_GLOB_IDX_NEGATIVE;
  if (index >= state->num_globals)
    return ERROR_GLOB_IDX_OUT_OF_RANGE;
  *ret_val = state->globals[index];
  return ERROR_NONE;
}
error_code_e state_set_glob(interpreter_state_t *state, uint32_t index,
                            aint value) {
  if (index < 0)
    return ERROR_GLOB_IDX_NEGATIVE;
  if (index >= state->num_globals)
    return ERROR_GLOB_IDX_OUT_OF_RANGE;
  state->globals[index] = value;
  return ERROR_NONE;
}