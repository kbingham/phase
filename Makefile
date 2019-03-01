all: phase run

CFLAGS=-g -O0

phase: phase.o

run:
	./phase
