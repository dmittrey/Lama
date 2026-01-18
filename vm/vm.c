#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <bytecode_file>\n", argv[0]);
    return 1;
  }

  return 0;
}