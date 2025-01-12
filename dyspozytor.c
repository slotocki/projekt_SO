#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include "common.h"
#include "semafor.h"
#include "colors.h"

static volatile sig_atomic_t stopGen=0;
static void handler_int(int s){
    stopGen=1;
    fprintf(stderr,"[Dyspozytor] Otrzymano SIGINT => przerwanie generowania pasażerów.\n");
}
static void handler_sigusr2(int s){
    stopGen=1;
    fprintf(stderr,"[Dyspozytor] Otrzymano SIGUSR2 => stop_odprawa.\n");
}
static void sigchld_handler(int s){
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main(){
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("[Dyspozytor] sigaction");
        exit(1);
    }
    
    signal(SIGINT, handler_int);
    signal(SIGUSR2, handler_sigusr2);

    srand(time(NULL));

    // 1. Pamięć
    int shm_id = utworz_pamiec_dzielona(KLUCZ_PAMIEC, sizeof(DaneWspolne));
    if(shm_id<0){
        perror("[Dyspozytor] shmget");
        exit(1);
    }
    DaneWspolne *wspolne = (DaneWspolne*) dolacz_pamiec_dzielona(shm_id);
    if((void*)wspolne == (void*)-1){
        perror("[Dyspozytor] shmat");
        exit(1);
    }

    // 2. Semafory
    int sem_count = 1 + LICZBA_STANOWISK + 2; 
    int semid = utworz_semafory(KLUCZ_SEMAFORY, sem_count);
    if(semid<0){
        perror("[Dyspozytor] semget");
        exit(1);
    }

    // inicjuj semafory
    inicjuj_semafor(semid, SEM_IDX_MUTEX, 1); 
    for(int i=0; i<LICZBA_STANOWISK; i++){
        inicjuj_semafor(semid, SEM_IDX_STANOWISKA + i, MAX_OSOBY_STANOWISKO);
    }
    // schody
    inicjuj_semafor(semid, SEM_IDX_SCHODY, 2);
    inicjuj_semafor(semid, SEM_IDX_POJEMNOSC, 5);

    semafor_op(semid, SEM_IDX_MUTEX, -1);

    wspolne->stop_odprawa=0;
    wspolne->aktualne_Md=10;
    wspolne->P=5;
    wspolne->K=2;
    wspolne->T1=20;
    wspolne->N=4;  
    wspolne->samoloty_skonczone=0;
    wspolne->stan_samolotu=SAMOLOT_OCZEKUJE;
    wspolne->liczba_pasazerow_w_samolocie=0;
    wspolne->liczba_pasazerow_na_schodach=0;

    for(int i=0;i<LICZBA_STANOWISK;i++){
        wspolne->stanowiska[i].ile_osob=0;
        wspolne->stanowiska[i].plec_obecna=-1;
    }

    wspolne->kolejka_vip.liczba=0;
    wspolne->kolejka_vip.pocz=0;
    wspolne->kolejka_vip.kon=0;
    wspolne->kolejka_normal.liczba=0;
    wspolne->kolejka_normal.pocz=0;
    wspolne->kolejka_normal.kon=0;

    wspolne->gate.vip_queue.liczba=0;
    wspolne->gate.vip_queue.pocz=0;
    wspolne->gate.vip_queue.kon=0;
    wspolne->gate.normal_queue.liczba=0;
    wspolne->gate.normal_queue.pocz=0;
    wspolne->gate.normal_queue.kon=0;
    wspolne->gate.open=false;

    wspolne->md_for_plane[0]=10;
    wspolne->md_for_plane[1]=12;
    wspolne->md_for_plane[2]=8;
    wspolne->md_for_plane[3]=11;
    wspolne->current_plane_idx=0;
    wspolne->gate_closed_for_this_plane=false;

    semafor_op(semid, SEM_IDX_MUTEX, 1);

    printf("[Dyspozytor] Parametry: P=5,K=2,T1=10,N=4.\n");
    printf("[Dyspozytor] md_for_plane=[10,12,8,11]\n");

    pid_t kapitan = fork();
    if(kapitan==0){
        execl("./kapitan_lotu", "kapitan_lotu", NULL);
        perror("[Dyspozytor] execl kapitan_lotu");
        exit(1);
    }
    printf(YELLOW "[Dyspozytor] Uruchomiono kapitan_lotu PID=%d\n" RESET, kapitan);

    // 5. Generowanie pasażerów
    while(!stopGen){
        // Co sekundę np. 2 pasażerów
        for(int i=0;i<2;i++){
            int plec= rand()%2; 
            bool danger=(rand()%10<2);  
            bool vip=(rand()%10<3);    

            char a1[8], a2[8], a3[8];
            snprintf(a1,sizeof(a1),"%d",plec);
            snprintf(a2,sizeof(a2),"%d",(danger?1:0));
            snprintf(a3,sizeof(a3),"%d",(vip?1:0));

            pid_t p = fork();
            if(p==0){
                execl("./pasazer", "pasazer", a1, a2, a3, NULL);
                perror("[Dyspozytor] execl pasazer");
                exit(1);
            }
        }
        sleep(1);
    }

    semafor_op(semid, SEM_IDX_MUTEX, -1);
    wspolne->stop_odprawa=1;
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    kill(kapitan, SIGINT);
    waitpid(kapitan, NULL, 0);

    // usuwanie pamięci i semaforów
    odlacz_pamiec_dzielonej(wspolne);
    usun_pamiec_dzielona(shm_id);
    usun_semafory(semid);

    printf("[Dyspozytor] Zakończono.\n");
    return 0;
}
