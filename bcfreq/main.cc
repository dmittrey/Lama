#include <cstdio>
#include <iostream>
#include <queue>
#include <string>
#include <unordered_set>

#include "bcfreq.hh"

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "Usage: %s <bytecode.bc>\n", argv[0]);
    return 1;
  }

  BytecodeFreq bcfreq(argv[1]);

  bcfreq.find_reachable_instructions();
  bcfreq.find_idioms();
}