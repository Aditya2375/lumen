CC      ?= cc
CFLAGS  ?= -std=gnu11 -O2 -g -Wall -Wextra -Werror -Isrc
BIN     := build/lumen
SRCS    := src/main.c src/vm.c src/asm.c

all: $(BIN)

$(BIN): $(SRCS) src/lumen.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRCS) -o $@

check: $(BIN)
	./tests/run-tests.sh

clean:
	rm -rf build

.PHONY: all check clean
