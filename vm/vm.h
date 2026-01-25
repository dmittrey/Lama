#ifndef VM_H
#define VM_H

typedef enum error_code_e {
  ERROR_NONE = 0,
  ERROR_STOP = 1,
} error_code_e;

void interpret_bc(FILE *f, interpreter_state_t *state,
                  error_code_e *error_code);

#endif