.PHONY: all debug

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    CC=gcc
else ifeq ($(UNAME_S),Darwin)
    CC=clang
    ARCH = -arch x86_64
endif

DISABLE_WARNINGS=-Wno-shift-negative-value
COMMON_FLAGS=$(DISABLE_WARNINGS) -g -fstack-protector-all $(ARCH) -I. -I../runtime -I../byterun
COMMON_CFLAGS=$(COMMON_FLAGS) -DLAMA_ENV
PROD_FLAGS=$(COMMON_CFLAGS)
DEBUG_FLAGS=$(COMMON_CFLAGS) -DDEBUG

OBJS=vmver.o verify.o bytefile.o ../runtime/runtime.a

# Sanitizer flags
ASAN_FLAGS=-fsanitize=address -fno-omit-frame-pointer
UBSAN_FLAGS=-fsanitize=undefined
MSAN_FLAGS=-fsanitize=memory -fno-omit-frame-pointer

all: vmver

debug: vmver-allsan

vmver: $(OBJS)
	$(CC) $(PROD_FLAGS) -o vmver $(OBJS) -lc++

%.o: %.c
	$(CC) $(PROD_FLAGS) -c $< -o $@

bytefile.o: ../byterun/bytefile.c
	$(CC) $(COMMON_CFLAGS) -c $< -o $@

vmver-asan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(ASAN_FLAGS) -o vmver-asan $(OBJS) -lc++

vmver-ubsan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(UBSAN_FLAGS) -o vmver-ubsan $(OBJS) -lc++

vmver-allsan: $(OBJS)
	$(CC) $(DEBUG_FLAGS) $(ASAN_FLAGS) $(UBSAN_FLAGS) -o vmver-allsan $(OBJS) -lc++

clean:
	$(RM) vmver vmver-asan vmver-ubsan vmver-msan vmver-allsan *.o *~

.PHONY: all clean vmver-asan vmver-ubsan vmver-msan vmver-allsan