#ifndef CALLSTACK_H
#define CALLSTACK_H

#include <stdint.h>
#include <stddef.h>

#define CALLSTACK_INITIAL_SIZE 64
#define CALLSTACK_MAX_SIZE     1024

/* Call frame structure */
typedef struct {

} call_frame_t;

typedef struct {
    call_frame_t *frames;  /* call frames */
    size_t size;          /* current number of frames */
    size_t capacity;      /* allocated capacity */
} callstack_t;

callstack_t* create_callstack();

void destroy_callstack(callstack_t*);

#endif