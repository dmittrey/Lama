#include "state.h"

#include <stdio.h>
#include <stdlib.h>

interpreter_state_t *create_interpreter_state(bytefile *bf) {
  interpreter_state_t *state = malloc(sizeof(interpreter_state_t));
  if (!state) {
    fprintf(stderr, "Failed to allocate interpreter state\n");
    return NULL;
  }

  /* Create call stack */
  state->callstack = create_callstack();
  if (!state->callstack) {
    fprintf(stderr, "Failed to create call stack\n");
    free(state);
    return NULL;
  }

  /* Virtual registers */
  state->ip = bf->code_ptr; /* address of current instruction */

  /* Memory areas */
  state->num_globals = bf->global_area_size;
  if (state->num_globals > 0) {
    state->globals = calloc(state->num_globals, sizeof(int32_t));
    if (!state->globals) {
      fprintf(stderr, "Failed to allocate global variables\n");
      destroy_callstack(state->callstack);
      free(state);
      return NULL;
    }
  } else {
    state->globals = NULL;
  }

  /* Bytecode file */
  state->bf = bf;

  return state;
}

void destroy_interpreter_state(interpreter_state_t *state) {
  if (state) {
    if (state->callstack) {
      destroy_callstack(state->callstack);
    }
    if (state->globals) {
      free(state->globals);
    }
    free(state);
  }
}