# Makefile for HW 2. Ben Foxman netid-btf28
CC=gcc
CFLAGS= -std=c99 -pedantic -Wall -g3
#HWK2_SRC= . # for local use
HWK2_SRC= /c/cs323/Hwk2 # for use on zoo

all:  Parsley MainParsley

Parsley:  parsley.o mainParsley.o
	${CC} ${CFLAGS} -o $@ $^

MainParsley: $(HWK2_SRC)/mainParsley.o

mainParsley.o: $(HWK2_SRC)/parsley.h

clean:
	rm -f Parsley *.o