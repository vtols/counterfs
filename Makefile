all:
	gcc counterfs.c -Wall -o counterfs `pkg-config fuse --cflags --libs`
