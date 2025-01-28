#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "common.h"
#include "semafor.h"
#include "colors.h"

static volatile sig_atomic_t stopKap = 0;
static volatile sig_atomic_t startWczesniej = 0;

/* Semafor, aby móc w wybudzić czekającego kapitana */
static int semid_global = -1;

static void sigint_handler(int s){ 
    stopKap = 1; 
    if(semid_global != -1) {
        semafor_op(semid_global, SEM_IDX_START_EARLIER, 1);
    }
}

static void sigusr1_handler(int s){ 
    startWczesniej = 1; 
    fprintf(stderr,"[Kapitan] sygnal1 => start wcześniej.\n");
    if(semid_global != -1) {
        semafor_op(semid_global, SEM_IDX_START_EARLIER, 1);
    }
}

int main(int argc, char *argv[]){
    signal(SIGINT, sigint_handler);
    signal(SIGUSR1, sigusr1_handler);

    if(argc<2){
        fprintf(stderr,"[Kapitan] Brak indeksu samolotu w argv!\n");
        exit(1);
    }
    int plane_idx = atoi(argv[1]);

    srand(time(NULL)^getpid());

    /*  dostęp do pamięci dzielonej */
    int shm_id = utworz_pamiec_dzielona(KLUCZ_PAMIEC, sizeof(DaneWspolne));
    if(shm_id<0){ perror("[Kapitan] shmget"); exit(1); }
    DaneWspolne *wspolne = (DaneWspolne*) dolacz_pamiec_dzielona(shm_id);
    if((void*)wspolne==(void*)-1){ perror("[Kapitan] shmat"); exit(1); }

    int sem_count = SEM_COUNT; 
    int semid   = utworz_semafory(KLUCZ_SEMAFORY, sem_count);
    semid_global = semid; 

    /* dostęp do kolejki komunikatów VIP */
    int vip_qid = msgget(VIP_QUEUE_KEY, 0666);
    if(vip_qid == -1) {
        perror("[Kapitan] msgget VIP queue");

        exit(1);
    }

    /* ustawiamy samolot w stan OCZEKUJE */
    semafor_op(semid, SEM_IDX_MUTEX, -1);
    wspolne->samoloty[plane_idx].stan = SAMOLOT_OCZEKUJE;
    int cur_md  = wspolne->aktualne_Md;
    int p = P_VALUE;
    wspolne->stan_samolotu = SAMOLOT_OCZEKUJE;
    /* inicjujemy semafor pojemności (P) - na wszelki wypadek */
    inicjuj_semafor(semid, SEM_IDX_POJEMNOSC, p);
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    semafor_op(semid, SEM_IDX_MUTEX, -1);
    printf("[Kapitan] Przywracam VIP-y z kolejki komunikatów (Md=%d, samolot=%d)...\n",
           cur_md, plane_idx);
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    while(!stopKap) {
        VipKolejkaKom vmsg;
        ssize_t rcv_bytes = msgrcv(vip_qid, &vmsg, sizeof(vmsg) - sizeof(long), 0, IPC_NOWAIT);
        if(rcv_bytes == -1) {
            if(errno == ENOMSG) {
                /* Nie ma więcej VIP-ów w kolejce, kończymy przywracanie */
                break;
            } else {
                perror("[Kapitan] msgrcv VIP queue");
                break;
            }
        }
        /* Dodajemy pasażera do kolejki VIP */
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        pushKolejka(&wspolne->gate.vip_queue, vmsg.vip_elem);
        printf("[Kapitan] Przywracam VIP-a %d do kolejki VIP (Md=%d, samolot=%d).\n",
               vmsg.vip_elem.pid, cur_md, plane_idx);
        semafor_op(semid, SEM_IDX_MUTEX, 1);
    }

    /* Kapitan czeka na sygnał (SIGUSR1), start */
    while(!stopKap && !startWczesniej){
        semafor_op(semid, SEM_IDX_START_EARLIER, -1);
    }

    if(stopKap) {
        printf("[Kapitan] Otrzymano SIGINT => rezygnuję (samolot %d).\n", plane_idx);
        odlacz_pamiec_dzielonej(wspolne);
        return 0;
    }

    /* Samolot przechodzi w stan LECI + informacja do Dyspozytora */
    semafor_op(semid, SEM_IDX_MUTEX, -1);
    wspolne->samoloty[plane_idx].stan = SAMOLOT_LECI;
    wspolne->stan_samolotu           = SAMOLOT_LECI;
    wspolne->samolot_ktory_wystartowal = plane_idx;
    int w_sam = wspolne->samoloty[plane_idx].liczba_pasazerow;
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    /* Sygnalizujemy, że samolot "wystartował" */
    semafor_op(semid, SEM_IDX_TAKEOFF_DONE, 1);

    printf(YELLOW "[Kapitan] Start samolotu %d!"RESET" (w_sam=%d, P=%d, md=%d)\n",
           plane_idx, w_sam, p, cur_md);

    /* Czekamy aż pasażerowie skończą wchodzić po schodach => SEM_IDX_SCHODY_OK */
    bool schodyOk = false;
    while(!stopKap && !schodyOk){
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        if(wspolne->liczba_pasazerow_na_schodach==0) { 
            schodyOk=true; 
        }
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if(!schodyOk) {
            semafor_op(semid, SEM_IDX_SCHODY_OK, -1);
        }
    }
    if(stopKap) {
        printf("[Kapitan] STOP w trakcie czekania na schody (samolot %d).\n", plane_idx);
        odlacz_pamiec_dzielonej(wspolne);
        return 0;
    }

    /* Lot trwa 15..17s */
    int czasLotu = 15 + (rand()%3);
    sleep(czasLotu); //niegroźny
    printf(YELLOW"[Kapitan] Samolot %d ląduje " RESET" (czasLotu=%d)\n", plane_idx, czasLotu);

    /* Kończymy lot: samolot wraca, sygnalizujemy dostępność  */
    semafor_op(semid, SEM_IDX_MUTEX, -1);
    wspolne->samoloty[plane_idx].stan = SAMOLOT_WROCIL;
    wspolne->samoloty[plane_idx].liczba_pasazerow = 0; 
    wspolne->samoloty_skonczone++;
    int sc2 = wspolne->samoloty_skonczone;
    printf("[Kapitan] Samolot %d zakończył lot => sc=%d\n", plane_idx, sc2);

    /* do Dyspozytora, że samolot jest dostępny ponownie */
    semafor_op(semid, SEM_IDX_PLANE_AVAILABLE, 1);
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    printf("[Kapitan] Kończę pracę (samolot=%d).\n", plane_idx);
    odlacz_pamiec_dzielonej(wspolne);
    return 0;
}
