#include <stdlib.h>

#include "opstack.h"

opstack_t* create_opstack()
{
    return malloc(sizeof(opstack_t));
}

void destroy_opstack(opstack_t* st)
{
    free(st);
}