.PHONY: all debug

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    CC=gcc
else ifeq ($(UNAME_S),Darwin)
    CC=clang
    ARCH = -arch x86_64
endif

DISABLE_WARNINGS=-Wno-shift-negative-value
COMMON_FLAGS=$(DISABLE_WARNINGS) -g -fstack-protector-all $(ARCH) --std=c11 -I. -I../runtime -I../byterun
PROD_FLAGS=$(COMMON_FLAGS) -DLAMA_ENV
DEBUG_FLAGS=$(COMMON_FLAGS) -DLAMA_ENV -DDEBUG

OBJS=vmver.c bytefile.o ../runtime/runtime.a

# Sanitizer flags
ASAN_FLAGS=-fsanitize=address -fno-omit-frame-pointer
UBSAN_FLAGS=-fsanitize=undefined
MSAN_FLAGS=-fsanitize=memory -fno-omit-frame-pointer

all: vmver

debug: vmver-allsan

vmver: $(OBJS)
	$(CC) $(PROD_FLAGS) -o vmver $(OBJS)

bytefile.o: ../byterun/bytefile.c
	$(CC) $(PROD_FLAGS) -c $< -o $@

%.o: %.cc
	$(CC) $(PROD_FLAGS) -c $< -o $@

vmver-asan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(ASAN_FLAGS) -o vmver-asan $(OBJS)

vmver-ubsan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(UBSAN_FLAGS) -o vmver-ubsan $(OBJS)

vmver-msan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(MSAN_FLAGS) -o vmver-msan $(OBJS)

vmver-allsan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(ASAN_FLAGS) $(UBSAN_FLAGS) -o vmver-allsan $(OBJS)

clean:
	$(RM) vmver vmver-asan vmver-ubsan vmver-msan vmver-allsan *~

.PHONY: all clean vmver-asan vmver-ubsan vmver-msan vmver-allsan