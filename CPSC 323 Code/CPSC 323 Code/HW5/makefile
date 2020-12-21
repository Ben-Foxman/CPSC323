# Ben Foxman, netid=btf28
# Makefile for HW5-bashLT

CC=gcc
CFLAGS=-g3 -std=c99 -pedantic -Wall
HWK5=/c/cs323/Hwk5

all: bashLT  

bashLT: $(HWK5)/mainBashLT.o $(HWK5)/parsley.o process.o
	${CC} ${CFLAGS} -o $@ $^


submit:
	/c/cs323/bin/submit 5 log.bashLT process.c makefile
