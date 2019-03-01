all: phase run

CFLAGS=-g -O0

phase: main.o phase.o

run:
	./phase
