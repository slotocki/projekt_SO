CC     = gcc
CFLAGS = -Wall -Wextra -std=c99

all: dyspozytor kapitan_lotu pasazer


dyspozytor: dyspozytor.o semafor.o
	$(CC) $(CFLAGS) -o $@ $^

dyspozytor.o: dyspozytor.c common.h semafor.h
	$(CC) $(CFLAGS) -c dyspozytor.c


kapitan_lotu: kapitan_lotu.o semafor.o
	$(CC) $(CFLAGS) -o $@ $^

kapitan_lotu.o: kapitan_lotu.c common.h semafor.h
	$(CC) $(CFLAGS) -c kapitan_lotu.c


pasazer: pasazer.o semafor.o
	$(CC) $(CFLAGS) -o $@ $^

pasazer.o: pasazer.c common.h semafor.h
	$(CC) $(CFLAGS) -c pasazer.c


semafor.o: semafor.c semafor.h common.h
	$(CC) $(CFLAGS) -c semafor.c


clean:
	rm -f *.o dyspozytor kapitan_lotu pasazer
