#gcc -Wall -o lsh -pthread shellex.c LinkedList.c csapp.c

CC=gcc
CFLAGS=-Wall

lsh: shellex.c LinkedList.h LinkedList.c csapp.h csapp.c
	gcc -o ../install/bin/lsh -pthread shellex.c LinkedList.c csapp.c

CLEAN:
	rm -f ../install/bin/lsh *.o
