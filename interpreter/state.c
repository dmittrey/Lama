#include "state.h"

#include <stdio.h>
#include <stdlib.h>

interpreter_state_t *create_interpreter_state(bytefile *bf, size_t stack_size) {
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

    /* Create operand stack */
    state->opstack = create_opstack();
    if (!state->opstack) {
        fprintf(stderr, "Failed to create operand stack\n");
        destroy_callstack(state->callstack);
        free(state);
        return NULL;
    }

    /* Virtual registers */
    state->ip = bf->code_ptr;           /* start at beginning of code */

    /* Bytecode file */
    state->bf = bf;

    return state;
}

void destroy_interpreter_state(interpreter_state_t *state) {
    if (state) {
        if (state->callstack) {
            destroy_callstack(state->callstack);
        }
        if (state->opstack) {
            destroy_opstack(state->opstack);
        }
        free(state);
    }
}