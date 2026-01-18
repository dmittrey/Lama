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

all: interpreter

interpreter: iter_sm.c ../runtime/runtime.a
	$(CC) $(PROD_FLAGS) -o interpreter interpreter.c ../runtime/runtime.a

clean:
	$(RM) interpreter interpreter.out *~

.PHONY: all clean