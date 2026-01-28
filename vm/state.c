#include "state.h"
#include "bytecode.h"
#include "callstack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Structure internals */
typedef struct interpreter_state_t {
  /* Virtual registers */
  char *ip; /* address of current instruction */

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

  /* Create call stack */
  cs_init(bf->global_area_size);

  /* Bytecode file */
  state->bf = bf;

  return state;
}
void destroy_interpreter_state(interpreter_state_t *state) {
  if (state) {
    cs_shutdown();
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
int32_t state_read_int(interpreter_state_t *state) {
  int32_t value = 0;
  memcpy(&value, state->ip, sizeof(value));
  state->ip += sizeof(value);
  return value;
}
char state_read_byte(interpreter_state_t *state) {
  return (unsigned char)*state->ip++;
}
char *state_read_string(interpreter_state_t *state) {
  return get_string(state->bf, state_read_int(state));
}
error_code_e state_jmp(interpreter_state_t *state, int32_t offset) {
  if (offset < 0) {
    fprintf(stderr, "vm: jump offset negative: %d at ip=0x%.8lx\n", offset,
            state->ip - state->bf->code_ptr - 1);
    return ERROR_JUMP_OFFSET_NEGATIVE;
  }

  if (offset >= state->bf->code_size) {
    fprintf(stderr,
            "vm: jump offset out of range: %d(code size=%lu) at ip=0x%.8lx\n",
            offset, state->bf->code_size, state->ip - state->bf->code_ptr - 1);
    return ERROR_JUMP_OFFSET_OUT_OF_RANGE;
  }

  state->ip = state->bf->code_ptr + offset;
  return ERROR_NONE;
}
