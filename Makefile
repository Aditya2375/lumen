CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Werror -Isrc
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
