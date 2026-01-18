#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../runtime/runtime.h"


int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <bytecode_file>\n", argv[0]);
        return 1;
    }

    return 0;
}