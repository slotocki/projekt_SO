# Kompilator i flagi
CC     = gcc
CFLAGS = -Wall -Wextra -std=c99

# Nazwy docelowych programów
TARGETS = dyspozytor kapitan_lotu pasazer

# Pliki źródłowe
DYSP_SRC = dyspozytor.c semafor.c common.c
KAPT_SRC = kapitan_lotu.c semafor.c common.c
PASZ_SRC = pasazer.c semafor.c common.c

# Domyślny cel
all: $(TARGETS)

# Reguły budowania poszczególnych programów
dyspozytor: $(DYSP_SRC)
	$(CC) $(CFLAGS) -o $@ $^

kapitan_lotu: $(KAPT_SRC)
	$(CC) $(CFLAGS) -o $@ $^

pasazer: $(PASZ_SRC)
	$(CC) $(CFLAGS) -o $@ $^

# Cel czyszczący
clean:
	rm -f $(TARGETS)

# Określenie celów specjalnych
.PHONY: all clean
