BINS = bfs mkfs
CC = gcc
CFLAGS = -Wall -Werror -pedantic -std=gnu18 -g
FUSE_CFLAGS = `pkg-config fuse --cflags --libs`

SRC = bfs.c fs_logic.c fuse_op.c
HEADER = bfs.h
DISK = mkfs.c


.PHONY: all
all: $(BINS)

bfs:
	$(CC) $(CFLAGS) $(SRC) $(HEADER) $(FUSE_CFLAGS) -o bfs
mkfs:
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -o mkfs $(DISK)

.PHONY: clean
clean:
	rm -rf $(BINS)
