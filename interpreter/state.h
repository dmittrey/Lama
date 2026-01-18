#ifndef STATE_H
#define STATE_H

#include <stddef.h>

#include "bytecode.h"
#include "callstack.h"
#include "opstack.h"

/* Interpreter state structure */
typedef struct {
    /* Virtual registers */
    char *ip;          /* address of current instruction */
    char *sp;          /* call stack ptr */
    char *fp;          /* current call stack frame */

    /* Call stack */
    callstack_t *callstack;

    /* Operands stack */
    opstack_t *opstack;

    /* Bytecode file */
    bytefile *bf;      /* loaded bytecode file */
} interpreter_state_t;

interpreter_state_t *create_interpreter_state(bytefile *bf, size_t stack_size);

void destroy_interpreter_state(interpreter_state_t *state);

#endif

