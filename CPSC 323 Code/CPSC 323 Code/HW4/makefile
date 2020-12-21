# Ben Foxman, netid=btf28
# Makefile for HW4-lzw
CC=gcc
CFLAGS=-g3 -std=c99 -pedantic -Wall
HWK4=/c/cs323/Hwk4
all: encode decode

lzw:  $(HWK4)/code.o lzw.c
	${CC} ${CFLAGS} $^ -o lzw

encode: lzw
	ln -f lzw encode

decode: lzw
	ln -f lzw decode

submit:
	/c/cs323/bin/submit 4 log.lzw lzw.c makefile


