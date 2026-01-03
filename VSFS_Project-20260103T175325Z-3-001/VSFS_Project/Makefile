CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g

TARGETS = mkfs journal validator
SRCS = mkfs.c journal.c validator.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run test

all: $(TARGETS)

mkfs: mkfs.o
	$(CC) $(CFLAGS) -o $@ $^

journal: journal.o
	$(CC) $(CFLAGS) -o $@ $^

validator: validator.o
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGETS) vsfs.img

run: all
	./mkfs
	./validator

test: all
	@echo "=== Creating fresh filesystem ==="
	./mkfs
	@echo ""
	@echo "=== Validating initial filesystem ==="
	./validator
	@echo ""
	@echo "=== Creating file 'test1' ==="
	./journal create test1
	@echo ""
	@echo "=== Validating before install ==="
	./validator || true
	@echo ""
	@echo "=== Installing changes ==="
	./journal install
	@echo ""
	@echo "=== Validating after install ==="
	./validator
	@echo ""
	@echo "=== Creating file 'test2' ==="
	./journal create test2
	@echo ""
	@echo "=== Creating file 'test3' ==="
	./journal create test3
	@echo ""
	@echo "=== Validating before second install ==="
	./validator || true
	@echo ""
	@echo "=== Installing changes ==="
	./journal install
	@echo ""
	@echo "=== Validating after second install ==="
	./validator
	@echo ""
	@echo "=== Test complete ==="
