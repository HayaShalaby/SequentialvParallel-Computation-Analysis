all: main

# 1. Change the compiler to the C compiler (gcc)
CC = gcc
override CFLAGS += -g -Wno-everything

# 2. Change SRCS to find files ending in .c instead of .cpp
SRCS = $(shell find . -name '.ccls-cache' -type d -prune -o -type f -name '*.c' -print | sed -e 's/ /\\ /g')
HEADERS = $(shell find . -name '.ccls-cache' -type d -prune -o -type f -name '*.h' -print)

main: $(SRCS) $(HEADERS)
	# 3. Use the C compiler variable (CC) and C flags (CFLAGS)
	$(CC) $(CFLAGS) $(SRCS) -o "$@"

main-debug: $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -O0 $(SRCS) -o "$@"

clean:
	rm -f main main-debug