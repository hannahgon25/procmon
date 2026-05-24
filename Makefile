CC = gcc
CFLAGS = -Wall -O2
LIBS = -lncurses

procmon: procmon.c
	$(CC) $(CFLAGS) -o procmon procmon.c $(LIBS)

clean:
	rm -f procmon
