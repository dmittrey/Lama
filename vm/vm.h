#ifndef VM_H
#define VM_H

#include "error.h"

void interpret_bc(FILE *f, struct interpreter_state_t *state,
                  error_code_e *error_code);

#endif