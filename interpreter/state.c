#include "state.h"

#include <stdio.h>
#include <stdlib.h>

interpreter_state_t *create_interpreter_state(bytefile *bf, size_t stack_size) {
    interpreter_state_t *state = malloc(sizeof(interpreter_state_t));
    if (!state) {
        fprintf(stderr, "Failed to allocate interpreter state\n");
        return NULL;
    }

    /* Call stack */
    state->sp = malloc(stack_size);
    if (!state->sp) {
        fprintf(stderr, "Failed to allocate call stack\n");
        free(state);
        return NULL;
    }

    /* Virtual registers */
    state->ip = bf->code_ptr;           /* start at beginning of code */
    state->fp = state->sp;              /* frame pointer at stack base */

    /* Bytecode file */
    state->bf = bf;

    return state;
}

void destroy_interpreter_state(interpreter_state_t *state) {
    if (state) {
        free(state->sp);
        free(state);
    }
}