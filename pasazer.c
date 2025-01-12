#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <assert.h>

#include "common.h"
#include "semafor.h"
#include "colors.h"

static volatile sig_atomic_t stopSig = 0;
static void sig_stop(int s) {
    stopSig = 1;
}

static void pushKolejka(Kolejka *k, ElemKolejki e) {
    if(k->liczba < ROZMIAR_KOLEJKI){
        k->tab[k->kon] = e;
        k->kon = (k->kon + 1) % ROZMIAR_KOLEJKI;
        k->liczba++;
    }
}

int main(int argc, char *argv[]) {
    signal(SIGUSR2, sig_stop);
    srand(time(NULL) ^ getpid());

    int plec = 0, danger = 0, vip = 0;
    if(argc > 1) plec   = atoi(argv[1]);
    if(argc > 2) danger = atoi(argv[2]);
    if(argc > 3) vip    = atoi(argv[3]);

    bool is_female = (plec == 1);
    bool is_vip = (vip == 1);
    bool dangerous_item = (danger == 1);

    int shm_id = utworz_pamiec_dzielona(KLUCZ_PAMIEC, sizeof(DaneWspolne));
    if(shm_id < 0) {
        perror("[Pasażer] shmget");
        exit(1);
    }
    DaneWspolne *wspolne = (DaneWspolne*) dolacz_pamiec_dzielona(shm_id);
    if(wspolne == (void*) -1) {
        perror("[Pasażer] shmat");
        exit(1);
    }
    int sem_count = 1 + LICZBA_STANOWISK + 2;
    int semid = utworz_semafory(KLUCZ_SEMAFORY, sem_count);
    if(semid < 0) {
        perror("[Pasażer] semget");
        exit(1);
    }

    semafor_op(semid, SEM_IDX_MUTEX, -1);
    int stop_flag = wspolne->stop_odprawa;
    int Md = wspolne->aktualne_Md;
    semafor_op(semid, SEM_IDX_MUTEX, 1);
    if(stop_flag) {
        printf("[Pasażer %d] Odprawa zamknięta => rezygnuję.\n", getpid());
        goto KONIEC;
    }

    // Losujemy wagę bagażu
    int waga_bagazu = 6 + (rand() % 7);
    printf("[Pasażer %d] płeć=%s, VIP=%s, niebezp=%s, waga=%d\n",
        getpid(),
        (is_female ? "Kobieta" : "Mężczyzna"),
        (is_vip ? "TAK" : "NIE"),
        (dangerous_item ? "TAK" : "NIE"),
        waga_bagazu);

    // Odprawa bagażowa
    if(waga_bagazu > Md) {
        printf(RED "[Pasażer %d] Bagaż=%d > Md=%d => odrzucony.\n" RESET, getpid(), waga_bagazu, Md);
        goto KONIEC;
    }
    printf(GREEN "[Pasażer %d] Odprawa OK (bagaż <= %d)\n" RESET, getpid(), Md);

    ElemKolejki e;
    e.pid = getpid();
    e.plec = (is_female ? PLEC_KOBIETA : PLEC_MEZCZYZNA);
    e.vip = is_vip;
    e.dangerous_item = dangerous_item;
    e.przepuszczenia = 0;
    e.waga_bagazu = waga_bagazu;

    semafor_op(semid, SEM_IDX_MUTEX, -1);
    pushKolejka(&wspolne->kolejka_normal, e);
    printf("[Pasażer %d] Dołączam do kolejki normal.\n", getpid());
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    printf("[Pasażer %d] Dotarłem do hali odlotów. Kończę pracę.\n", getpid());

KONIEC:
    odlacz_pamiec_dzielonej(wspolne);
    return 0;
}
