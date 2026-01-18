UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    CC=gcc
else ifeq ($(UNAME_S),Darwin)
    CC=clang
    ARCH = -arch x86_64
endif

DISABLE_WARNINGS=-Wno-shift-negative-value
COMMON_FLAGS=$(DISABLE_WARNINGS) -g -fstack-protector-all $(ARCH) --std=c11 -I../runtime
PROD_FLAGS=$(COMMON_FLAGS) -DLAMA_ENV

all: interpreter.o bytecode.o state.o callstack.o opstack.o
	$(CC) $(PROD_FLAGS) -o interpreter main.c ../runtime/runtime.a interpreter.o bytecode.o state.o callstack.o opstack.o

interpreter.o: interpreter.c interpreter.h
	$(CC) $(PROD_FLAGS) -c interpreter.c -o interpreter.o

bytecode.o: bytecode.c bytecode.h
	$(CC) $(PROD_FLAGS) -c bytecode.c -o bytecode.o

state.o: state.c state.h
	$(CC) $(PROD_FLAGS) -c state.c -o state.o

callstack.o: callstack.c callstack.h
	$(CC) $(PROD_FLAGS) -c callstack.c -o callstack.o

opstack.o: opstack.c opstack.h
	$(CC) $(PROD_FLAGS) -c opstack.c -o opstack.o

clean:
	$(RM) interpreter *.o *~

.PHONY: all clean