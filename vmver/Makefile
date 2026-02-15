.PHONY: all debug

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    CC=gcc
    CXX=g++
else ifeq ($(UNAME_S),Darwin)
    CC=clang
    CXX=clang++
    ARCH = -arch x86_64
endif

DISABLE_WARNINGS=-Wno-shift-negative-value
COMMON_FLAGS=$(DISABLE_WARNINGS) -g -fstack-protector-all $(ARCH) -I. -I../runtime -I../byterun
COMMON_CFLAGS=$(COMMON_FLAGS) --std=c11 -DLAMA_ENV
COMMON_CXXFLAGS=$(COMMON_FLAGS) --std=c++17 -DLAMA_ENV
PROD_FLAGS=$(COMMON_CFLAGS)
DEBUG_FLAGS=$(COMMON_CFLAGS) -DLAMA_ENV -DDEBUG
PROD_CXXFLAGS=$(COMMON_CXXFLAGS)
DEBUG_CXXFLAGS=$(COMMON_CXXFLAGS) -DDEBUG

OBJS=vmver.o verify.o bytefile.o ../runtime/runtime.a

# Sanitizer flags
ASAN_FLAGS=-fsanitize=address -fno-omit-frame-pointer
UBSAN_FLAGS=-fsanitize=undefined
MSAN_FLAGS=-fsanitize=memory -fno-omit-frame-pointer

debug: vmver-allsan

all: vmver

vmver: $(OBJS)
	$(CXX) $(PROD_CXXFLAGS) -o vmver $(OBJS) -lc++

vmver.o: vmver.c
	$(CC) $(PROD_FLAGS) -c $< -o $@

verify.o: verify.cc
	$(CXX) $(PROD_CXXFLAGS) -c $< -o $@

bytefile.o: ../byterun/bytefile.c
	$(CC) $(PROD_FLAGS) -c $< -o $@

%.o: %.cc
	$(CXX) $(PROD_CXXFLAGS) -c $< -o $@

vmver-asan: $(OBJS)
	$(CXX) $(DEBUG_CXXFLAGS) $(ASAN_FLAGS) -o vmver-asan $(OBJS) -lc++

vmver-ubsan: $(OBJS)
	$(CXX) $(DEBUG_CXXFLAGS) $(UBSAN_FLAGS) -o vmver-ubsan $(OBJS) -lc++

vmver-msan: $(OBJS)
	$(CXX) $(DEBUG_CXXFLAGS) $(MSAN_FLAGS) -o vmver-msan $(OBJS) -lc++

vmver-allsan: clean $(OBJS)
	$(CXX) $(DEBUG_CXXFLAGS) $(ASAN_FLAGS) $(UBSAN_FLAGS) -o vmver-allsan $(OBJS) -lc++

clean:
	$(RM) vmver vmver-asan vmver-ubsan vmver-msan vmver-allsan vmver.o verify.o *~

.PHONY: debug all clean vmver-asan vmver-ubsan vmver-msan vmver-allsan