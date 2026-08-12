# Host-side tests for source/data, which depends on libc alone.

# Must be assigned, not defaulted: the parent Makefile exports CC as devkitARM's
# cross compiler, and inheriting it fails at the link with "cannot find -lasan".
HOSTCC  ?= cc
CC      := $(HOSTCC)

CFLAGS  := -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -I../source -I../vendor -I. \
           -fsanitize=address,undefined -fno-omit-frame-pointer

DATA_SRC := $(wildcard ../source/data/*.c) ../vendor/cjson/cJSON.c
TEST_SRC := $(wildcard *.c)

all: run

run: $(DATA_SRC) $(TEST_SRC)
	@$(CC) $(CFLAGS) -o $@ $(DATA_SRC) $(TEST_SRC)

.PHONY: all test clean
test: run
	@./run

clean:
	@rm -f run
