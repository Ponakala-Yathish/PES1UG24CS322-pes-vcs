CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g
LDFLAGS = -lcrypto

OBJS    = object.o tree.o index.o commit.o

.PHONY: all clean test-integration

all: pes test_objects test_tree

pes: pes.o $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_objects: test_objects.o object.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_tree: test_tree.o tree.o object.o index.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test-integration: pes
	@bash test_sequence.sh

clean:
	rm -f *.o pes test_objects test_tree
	rm -rf .pes
