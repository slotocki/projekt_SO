#include "common.h"
#include <stdio.h>

 // Pomocnicze funkcje do kolejki FIFO  
void pushKolejka(Kolejka *k, ElemKolejki e) {
    if (k->liczba < ROZMIAR_KOLEJKI) {
        k->tab[k->kon] = e;
        k->kon = (k->kon + 1) % ROZMIAR_KOLEJKI;
        k->liczba++;
    }
}

ElemKolejki frontKolejka(const Kolejka *k) {
    ElemKolejki e = {0};
    if (k->liczba > 0) {
        e = k->tab[k->pocz];
    }
    return e;
}

ElemKolejki popKolejka(Kolejka *k) {
    ElemKolejki e = {0};
    if (k->liczba > 0) {
        e = k->tab[k->pocz];
        k->pocz = (k->pocz + 1) % ROZMIAR_KOLEJKI;
        k->liczba--;
    }
    return e;
}

void moveFrontToBack(Kolejka *k) {
    if (k->liczba > 0) {
        ElemKolejki front = popKolejka(k);
        pushKolejka(k, front);
    }
}

void resetKolejka(Kolejka *k) {
    k->liczba = 0;
    k->pocz = 0;
    k->kon = 0;
}

void resetGate(Gate *g) {
    resetKolejka(&g->vip_queue);
    resetKolejka(&g->normal_queue);
    g->open = false;
}

void wyswietl_kolejke(const char* nazwa, const Kolejka *k) {
    printf("%s (liczba=%d): ", nazwa, k->liczba);
    for (int i = 0; i < k->liczba; i++) {
        int idx = (k->pocz + i) % ROZMIAR_KOLEJKI;
        printf("%d ", k->tab[idx].pid);
    }
    printf("\n");
}
