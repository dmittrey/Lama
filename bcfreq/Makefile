.PHONY: all debug

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    CC=g++
    CCC=gcc
else ifeq ($(UNAME_S),Darwin)
    CC=clang++
    CCC=clang
    ARCH = -arch x86_64
endif

DISABLE_WARNINGS=-Wno-shift-negative-value
COMMON_FLAGS=$(DISABLE_WARNINGS) -g -fstack-protector-all $(ARCH) -I. -I../runtime -I../byterun
COMMON_CFLAGS=$(COMMON_FLAGS) -DLAMA_ENV
COMMON_CXXFLAGS=$(COMMON_FLAGS) --std=c++17 -DLAMA_ENV
PROD_FLAGS=$(COMMON_CXXFLAGS)
DEBUG_FLAGS=$(COMMON_CXXFLAGS) -DDEBUG

OBJS=main.o bcfreq.o bytefile.o ../runtime/runtime.a

# Sanitizer flags
ASAN_FLAGS=-fsanitize=address -fno-omit-frame-pointer
UBSAN_FLAGS=-fsanitize=undefined
MSAN_FLAGS=-fsanitize=memory -fno-omit-frame-pointer

all: bcfreq

debug: bcfreq-allsan

bcfreq: $(OBJS)
	$(CC) $(PROD_FLAGS) -o bcfreq $(OBJS) -lc++

%.o: %.cc
	$(CC) $(PROD_FLAGS) -c $< -o $@

bytefile.o: ../byterun/bytefile.c
	$(CCC) $(COMMON_CFLAGS) -c $< -o $@

bcfreq-asan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(ASAN_FLAGS) -o bcfreq-asan $(OBJS) -lc++

bcfreq-ubsan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(UBSAN_FLAGS) -o bcfreq-ubsan $(OBJS) -lc++

bcfreq-allsan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(ASAN_FLAGS) $(UBSAN_FLAGS) -o bcfreq-allsan $(OBJS) -lc++

clean:
	$(RM) bcfreq bcfreq-asan bcfreq-ubsan bcfreq-msan bcfreq-allsan *.o *~

.PHONY: all clean bcfreq-asan bcfreq-ubsan bcfreq-msan bcfreq-allsan