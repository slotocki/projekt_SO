#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include "common.h"
#include "semafor.h"
#include "colors.h"

static volatile sig_atomic_t stopKap=0;
static volatile sig_atomic_t startEarlier=0;
static void sigint_handler(int s){ stopKap=1; }
static void sigusr1_handler(int s){ startEarlier=1; fprintf(stderr,"[Kapitan] sygnal1 => start wcześniej.\n"); }

int main(){
    signal(SIGINT, sigint_handler);
    signal(SIGUSR1, sigusr1_handler);

    srand(time(NULL)^getpid());

    int shm_id = utworz_pamiec_dzielona(KLUCZ_PAMIEC, sizeof(DaneWspolne));
    if(shm_id<0){
        perror("[Kapitan] shmget");
        exit(1);
    }
    DaneWspolne *wspolne = (DaneWspolne*) dolacz_pamiec_dzielona(shm_id);
    if((void*)wspolne==(void*)-1){
        perror("[Kapitan] shmat");
        exit(1);
    }

    int sem_count= 1 + LICZBA_STANOWISK + 2;
    int semid   = utworz_semafory(KLUCZ_SEMAFORY, sem_count);

    while(!stopKap){
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int sc = wspolne->samoloty_skonczone;
        int n  = wspolne->N;
        int idx= wspolne->current_plane_idx;
        if(sc>=n){
            semafor_op(semid, SEM_IDX_MUTEX, 1);
            printf("[Kapitan] Wszystkie samoloty (N=%d) zakończyły.\n", n);
            break;
        }
        int T1  = wspolne->T1;
        int p   = wspolne->P;
        int cur_md = wspolne->aktualne_Md;
        wspolne->stan_samolotu = SAMOLOT_OCZEKUJE;

        inicjuj_semafor(semid, SEM_IDX_POJEMNOSC, p);

        // otwieramy gate
        wspolne->gate.open=true;
        wspolne->gate_closed_for_this_plane=false;
        printf(YELLOW "[Kapitan] Gate otwarty dla samolotu %d (Md=%d)\n" RESET, idx, cur_md);
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        // czekamy T1 (lub sygnał)
        startEarlier=0;
        for(int s=0; s<T1; s++){
            if(stopKap || startEarlier) break;
            sleep(1);
        }
        if(stopKap) break;

        // zamykamy gate
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        wspolne->gate.open=false;
        wspolne->gate_closed_for_this_plane=true;
        printf("[Kapitan] Gate zamknięty dla samolotu %d (Md=%d)\n", idx, cur_md);
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        // czekamy aż schody puste
        while(!stopKap){
            semafor_op(semid, SEM_IDX_MUTEX, -1);
            int sch = wspolne->liczba_pasazerow_na_schodach;
            semafor_op(semid, SEM_IDX_MUTEX, 1);
            if(sch==0) break;
            sleep(1);
        }
        if(stopKap) break;

        // start
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int w_sam = wspolne->liczba_pasazerow_w_samolocie;
        printf("[Kapitan] Start! w_sam=%d (P=%d, md=%d)\n", w_sam, p, cur_md);
        wspolne->stan_samolotu=SAMOLOT_LECI;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        int czasLotu=10 + (rand()%3);
        sleep(czasLotu);
        printf("[Kapitan] Ląduję (czasLotu=%d)\n", czasLotu);

        // Po wylądowaniu
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        wspolne->stan_samolotu=SAMOLOT_WROCIL;
        wspolne->liczba_pasazerow_w_samolocie=0;
        wspolne->samoloty_skonczone++;
        sc= wspolne->samoloty_skonczone;
        wspolne->current_plane_idx++;
        int new_idx = wspolne->current_plane_idx;
        if(new_idx < wspolne->N){
            wspolne->aktualne_Md = wspolne->md_for_plane[new_idx];
            printf("[Kapitan] Samolot zakończył => sc=%d, kolejny Md=%d\n", sc, wspolne->aktualne_Md);
            wspolne->stan_samolotu=SAMOLOT_OCZEKUJE;
        } else {
            printf("[Kapitan] Samolot zakończył => sc=%d (brak kolejnych)\n", sc);
        }
        semafor_op(semid, SEM_IDX_MUTEX, 1);
    }

    printf("[Kapitan] Kończę pracę.\n");
    odlacz_pamiec_dzielonej(wspolne);
    return 0;
}
