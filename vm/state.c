#include "state.h"

#include <stdio.h>
#include <stdlib.h>

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

  /* Bytecode file */
  state->bf = bf;

  return state;
}
void destroy_interpreter_state(interpreter_state_t *state) {
  if (state) {
    if (state->globals) {
      free(state->globals);
    }

    free(state);
  }
}