// common.c
#include "common.h"
#include <stdio.h>

void wyswietl_kolejke(const char* nazwa, const Kolejka *k) {
    printf("Kolejka %s: [", nazwa);
    for(int i = 0; i < k->liczba; i++) {
        int idx = (k->pocz + i) % ROZMIAR_KOLEJKI;
        printf("%d", k->tab[idx].pid);
        if(i < k->liczba - 1) {
            printf(", ");
        }
    }
    printf("]\n");
}
