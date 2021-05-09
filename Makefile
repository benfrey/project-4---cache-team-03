CC=gcc

all: sim.o
	$(CC) sim.o -o sim

sim.o: sim.c
	$(CC) -c sim.c -o sim.o

clean:
	rm -Rf sim.o sim
