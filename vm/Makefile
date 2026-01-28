.PHONY: all debug

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

SRC=vm.c bytecode.c

# Sanitizer flags
ASAN_FLAGS=-fsanitize=address -fno-omit-frame-pointer
UBSAN_FLAGS=-fsanitize=undefined
MSAN_FLAGS=-fsanitize=memory -fno-omit-frame-pointer

all: vm

debug: vm-allsan

vm: vm.c ../runtime/runtime.a
	$(CC) $(PROD_FLAGS) -o vm $(SRC) ../runtime/runtime.a

vm-asan: vm.c ../runtime/runtime.a
	$(CC) $(PROD_FLAGS) $(ASAN_FLAGS) -o vm-asan $(SRC) ../runtime/runtime.a

vm-ubsan: vm.c ../runtime/runtime.a
	$(CC) $(PROD_FLAGS) $(UBSAN_FLAGS) -o vm-ubsan $(SRC) ../runtime/runtime.a

vm-msan: vm.c ../runtime/runtime.a
	$(CC) $(PROD_FLAGS) $(MSAN_FLAGS) -o vm-msan $(SRC) ../runtime/runtime.a

vm-allsan: vm.c ../runtime/runtime.a
	$(CC) $(PROD_FLAGS) $(ASAN_FLAGS) $(UBSAN_FLAGS) -o vm-allsan $(SRC) ../runtime/runtime.a

clean:
	$(RM) vm vm-asan vm-ubsan vm-msan vm-allsan *~

.PHONY: all clean vm-asan vm-ubsan vm-msan vm-allsan