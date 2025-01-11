#ifndef SEMAFOR_H
#define SEMAFOR_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

int   utworz_pamiec_dzielona(key_t klucz, size_t rozmiar);
void* dolacz_pamiec_dzielona(int shm_id);
void  odlacz_pamiec_dzielonej(const void *addr);
void  usun_pamiec_dzielona(int shm_id);

int   utworz_semafory(key_t klucz, int liczba);
void  inicjuj_semafor(int semid, int semnum, int wartosc);
void  semafor_op(int semid, int semnum, int operacja);
void  usun_semafory(int semid);

#endif
