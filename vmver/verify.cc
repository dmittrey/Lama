#include "verify.hh"
#include "verify_run.h"
#include <iostream>

extern "C" {

int run_verify(const char *path) {
  try {
    bytefile_ptr bf(read_file(const_cast<char *>(path)));
    if (!bf) {
      fprintf(stderr, "File not found!\n");
      return 1;
    }
    return analyse(std::move(bf));
  } catch (const std::runtime_error &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}

} /* extern "C" */
