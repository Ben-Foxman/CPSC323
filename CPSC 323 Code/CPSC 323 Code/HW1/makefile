# Benjamin Foxman makefile HW1 - netid btf28
CFLAGS = -g3 -std=c99 -pedantic -Wall

all:    fiend nLines

fiend:  fiend.o
	${CC} ${CFLAGS} $^ -o $@

nLines:
	/c/cs323/bin/xLines < fiend.c

clean:
	rm -f fiend *.o