CC=gcc -Wall -Werror -g

all: threads tls main.c
	$(CC) -o main main.c threads.o tls.o

threads: threads.c
	$(CC) -c -o threads.o threads.c

tls: tls.c
	$(CC) -c -o tls.o tls.c

clean:
	rm main threads.o tls.o
