#include <stdio.h>

#include "bytefile.h"

int main(int argc, char *argv[])
{
  struct bytefile *f = read_file(argv[1]);
  dump_file(stdout, f);
  return 0;
}
