#include <stdlib.h>

#include "callstack.h"

callstack_t* create_callstack()
{
    return malloc(sizeof(callstack_t));
}

void destroy_callstack(callstack_t* st)
{
    free(st);
}