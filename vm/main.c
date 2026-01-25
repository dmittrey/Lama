#include <stdio.h>
#include <stdlib.h>

#include "bytecode.h"
#include "state.h"
#include "vm.h"

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <bytecode_file>\n", argv[0]);
    return 1;
  }

  /* Load bytecode */
  bytefile *bf = parse_bc_file(argv[1]);
  if (!bf) {
    return 1;
  }

  /* Create interpreter state with 64KB stack */
  interpreter_state_t *state = create_interpreter_state(bf);
  if (!state) {
    fprintf(stderr, "Failed to create interpreter state\n");
    return 1;
  }

  /* Interpret bytecode */
  error_code_e error_code = ERROR_NONE;
  interpret_bc(stdout, state, &error_code);
  if (error_code != ERROR_NONE && error_code != ERROR_STOP) {
    fprintf(stderr, "Error: %d\n", error_code);
    return 1;
  }

  /* Cleanup interpreter state */
  destroy_interpreter_state(state);

  /* Cleanup */
  free(bf);

  return 0;
}