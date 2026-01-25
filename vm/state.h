#ifndef STATE_H
#define STATE_H

#include <stddef.h>
#include <stdint.h>

#include "../runtime/runtime.h"

#include "bytecode.h"

/* Interpreter state structure */
typedef struct interpreter_state_t {
  /* Virtual registers */
  char *ip; /* address of current instruction */

  /* Memory areas */
  aint *globals;      /* global variables array */
  size_t num_globals; /* number of global variables */

  /* Bytecode file */
  bytefile *bf; /* loaded bytecode file */
} interpreter_state_t;

/* Lifecycle */
struct interpreter_state_t *create_interpreter_state(bytefile *bf);
void destroy_interpreter_state(struct interpreter_state_t *state);

#endif
