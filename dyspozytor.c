#define _POSIX_C_SOURCE 200809L //makro funkcji preprocesora, dla SA_RESTART | SA_NOCLDSTOP

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "common.h"
#include "semafor.h"
#include "colors.h"

static volatile sig_atomic_t stopGen = 0; // zmienna bezpieczna dla sygnałów

static void handler_int(int s){
    stopGen = 1;
    fprintf(stderr,"[Dyspozytor] Otrzymano sygnał 2 => przerwanie generowania pasażerów.\n");
}

static void sigchld_handler(int s){
    while(waitpid(-1, NULL, WNOHANG) > 0) { }
}

void usun_ipcs(key_t key) {
    int semid = semget(key, 0, 0);
    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) {
            perror("semctl(IPC_RMID) failed");
        } else {
            printf("Semafor usunięty.\n");
        }
    }

    int shmid = shmget(key, 0, 0);
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1) {
            perror("shmctl(IPC_RMID) failed");
        } else {
            printf("Pamięć dzielona usunięta.\n");
        }
    }

    int msgid = msgget(key, 0);
    if (msgid != -1) {
        if (msgctl(msgid, IPC_RMID, NULL) == -1) {
            perror("msgctl(IPC_RMID) failed");
        } else {
            printf("Kolejka komunikatów usunięta.\n");
        }
    }
}

int main(){


     key_t klucz_semaforow = KLUCZ_SEMAFORY;
    key_t klucz_pamieci = KLUCZ_PAMIEC;
    key_t klucz_kolejki_vip = VIP_QUEUE_KEY;

    usun_ipcs(klucz_semaforow);
    usun_ipcs(klucz_pamieci);
    usun_ipcs(klucz_kolejki_vip);

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("[Dyspozytor] sigaction(SIGCHLD)");
        exit(1);
    }

    signal(SIGINT, handler_int);

    srand(time(NULL));

    /* Tworzymy i dołączamy pamięć dzieloną */
    int shm_id = utworz_pamiec_dzielona(KLUCZ_PAMIEC, sizeof(DaneWspolne));
    if(shm_id < 0){
        perror("[Dyspozytor] shmget");
        exit(1);
    }
    DaneWspolne *wspolne = (DaneWspolne*) dolacz_pamiec_dzielona(shm_id);
    if((void*)wspolne == (void*)-1){
        perror("[Dyspozytor] shmat");
        exit(1);
    }

    int sem_count = SEM_COUNT; 
    int semid = utworz_semafory(KLUCZ_SEMAFORY, sem_count);
    if(semid < 0){
        perror("[Dyspozytor] semget");
        exit(1);
    }

    int vip_qid = msgget(VIP_QUEUE_KEY, IPC_CREAT | 0666);
    if(vip_qid == -1) {
        perror("[Dyspozytor] msgget VIP queue");
        exit(1);
    }

    /* Inicjalizacja semaforów (raz na start) */
    inicjuj_semafor(semid, SEM_IDX_MUTEX, 1); 

    for(int i=0; i<LICZBA_STANOWISK; i++){
        inicjuj_semafor(semid, SEM_IDX_STANOWISKA + i, MAX_OSOBY_STANOWISKO);
    }
    // Na starcie ustawiamy SEM_IDX_POJEMNOSC na P_VALUE (15).
    // Ale w trakcie działania będziemy go ponownie inicjować przy każdym locie:
    inicjuj_semafor(semid, SEM_IDX_POJEMNOSC, P_VALUE);

    inicjuj_semafor(semid, SEM_IDX_SCHODY, 5);
    inicjuj_semafor(semid, SEM_IDX_VIP_WAIT, 0);
    inicjuj_semafor(semid, SEM_IDX_START_EARLIER, 0);
    inicjuj_semafor(semid, SEM_IDX_SCHODY_OK, 0);
    inicjuj_semafor(semid, SEM_IDX_PLANE_AVAILABLE, 0);
    inicjuj_semafor(semid, SEM_IDX_TAKEOFF_DONE, 0);
    inicjuj_semafor(semid, SEM_IDX_Q_KONTROLA_CHANGED, 0);
    inicjuj_semafor(semid, SEM_IDX_STANOWISKA_FREE, LICZBA_STANOWISK);
    inicjuj_semafor(semid, SEM_IDX_Q_KONTROLA_SPACE, ROZMIAR_KOLEJKI);
    inicjuj_semafor(semid, SEM_IDX_GATE_CHANGE, 0);

    /* Ustawienia w pamięci wspólnej */
    semafor_op(semid, SEM_IDX_MUTEX, -1);

    wspolne->stop_odprawa = 0;
    wspolne->aktualne_Md  = 12; 

    int p  = P_VALUE;  // maksymalna pojemność (15)
    int k  = K_VALUE;  // niewykorzystane w tym kodzie
    int t1 = T1_VALUE; // 20
    int t2 = T2_VALUE; // 10
    int n  = N_VALUE;  // 4

    wspolne->samoloty_skonczone = 0;
    wspolne->stan_samolotu = SAMOLOT_OCZEKUJE;
    wspolne->liczba_pasazerow_na_schodach = 0;

    for(int i=0; i<LICZBA_STANOWISK; i++){
        wspolne->stanowiska[i].ile_osob=0;
        wspolne->stanowiska[i].plec_obecna=-1;
    }

    resetKolejka(&wspolne->kolejka_kontrola);
    resetGate(&wspolne->gate);

    int maxObs = MAX_PASSENGERS; // 100
    wspolne->pasazerowieObsluzeni = 0;

    // Przykładowe Md dla 4 samolotów
    int md_values[N_VALUE] = {12, 12, 12, 11};
    for (int i = 0; i < N_VALUE; i++) {
        wspolne->md_for_plane[i] = md_values[i];
    }

    for(int i=0; i<n; i++){
        wspolne->samoloty[i].md   = wspolne->md_for_plane[i];
        wspolne->samoloty[i].stan = SAMOLOT_OCZEKUJE;
        wspolne->samoloty[i].idx  = i;
        wspolne->samoloty[i].kapitan_pid = -1;
        wspolne->samoloty[i].liczba_pasazerow = 0;
        // Każdy samolot "dostępny" => semafor PLANE_AVAILABLE
        semafor_op(semid, SEM_IDX_PLANE_AVAILABLE, 1);
    }

    wspolne->samolot_ktory_wystartowal = -1;
    wspolne->gate_closed_for_this_plane = false;

    int max_limit = 0;
    for(int i=0; i<n; i++){
        if(wspolne->md_for_plane[i] > max_limit){
            max_limit = wspolne->md_for_plane[i];
        }
    }
    wspolne->max_bagaz_limit = max_limit;
    wspolne->aktualny_samolot_idx = -1;

    semafor_op(semid, SEM_IDX_MUTEX, 1);

    printf("[Dyspozytor] Parametry: P=%d, K=%d, T1=%d, T2=%d, N=%d.\n",
           p, k, t1, t2, n);
    printf("[Dyspozytor] bagaż max=[10,12,10,11], max_bagaz_limit=%d\n", max_limit);

    /* Proces generujący pasażerów (potomny) */
    pid_t gen_pid = fork();
    if (gen_pid == 0) {
        int maxPasazerow = maxObs; 
        int wygenerowani = 0;

        while (!stopGen && wygenerowani < maxPasazerow) {
            // semafor_op_ret => nieblokujący semop
            if(semafor_op_ret(semid, SEM_IDX_Q_KONTROLA_SPACE, -1) == -1) {
                if(errno == EAGAIN || errno == EINTR) {
                    // FIFO pełne - odrzuć pasażera
                    printf("[Dyspozytor Generujący] FIFO pełne. Pasażer odrzucony.\n");

                    semafor_op(semid, SEM_IDX_MUTEX, -1);
                    wspolne->pasazerowieObsluzeni++;
                    semafor_op(semid, SEM_IDX_MUTEX, 1);

                    if(stopGen) break;
                    continue;
                } else {
                    perror("[Dyspozytor Generujący] SEM_IDX_Q_KONTROLA_SPACE");
                    break;
                }
            }

            if(stopGen) break;

            /* Generujemy parametry pasażera */
            int plec = rand() % 2;
            bool danger = (rand() % 10 < 2);
            bool vip = (rand() % 10 < 3);

            char a1[8], a2[8], a3[8];
            snprintf(a1, sizeof(a1), "%d", plec);
            snprintf(a2, sizeof(a2), "%d", (danger ? 1 : 0));
            snprintf(a3, sizeof(a3), "%d", (vip ? 1 : 0));

            pid_t px = fork();
            if (px == 0) {
                execl("./pasazer", "pasazer", a1, a2, a3, NULL);
                perror("[Dyspozytor Generujący] execl pasazer");
                exit(1);
            }

            wygenerowani++;
           sleep(1); // (opcjonalne opóźnienie generowania)
        }
        exit(0);
    }

    /* Główna pętla Dyspozytora */
    while(!stopGen) {
        /* Sprawdzamy, ilu pasażerów już obsłużono */
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int served = wspolne->pasazerowieObsluzeni;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if (served >= maxObs) {
            printf("[Dyspozytor] Osiągnięto limit %d obsłużonych pasażerów.\n", maxObs);
            stopGen = 1;
        }
        if(stopGen) break;

        // Czekamy na dostępny samolot
        semafor_op(semid, SEM_IDX_PLANE_AVAILABLE, -1);

        /* Po wybudzeniu – ponownie sprawdzamy dostępne samoloty */
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int dostepne = -1;
        int dostepne_count = 0;
        int dostepne_indices[N_VALUE];

        for(int i = 0; i < n; i++) {
            if(wspolne->samoloty[i].stan == SAMOLOT_OCZEKUJE 
               || wspolne->samoloty[i].stan == SAMOLOT_WROCIL) {
                dostepne_indices[dostepne_count++] = i;
            }
        }
        // Losowy wybór samolotu
        if(dostepne_count > 0) {
            int random_idx = rand() % dostepne_count; 
            dostepne = dostepne_indices[random_idx];
        }
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if(stopGen) break;
        if(dostepne < 0) {
            // Fałszywe wybudzenie
            continue;
        }

        /* Konfigurujemy wybrany samolot do lotu */
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int samolot_idx = dostepne;
        wspolne->aktualne_Md = wspolne->samoloty[samolot_idx].md;
        wspolne->samolot_ktory_wystartowal = -1;
        wspolne->gate.open = false;
        
        wspolne->gate_closed_for_this_plane = false;
        wspolne->samoloty[samolot_idx].liczba_pasazerow = 0;
        wspolne->liczba_pasazerow_na_schodach = 0;
        wspolne->stan_samolotu = SAMOLOT_OCZEKUJE;
        wspolne->samoloty[samolot_idx].stan = SAMOLOT_OCZEKUJE;
        wspolne->aktualny_samolot_idx = samolot_idx;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        /* Uruchomienie kapitana */
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int obsluzeni_do_tej_pory = wspolne->pasazerowieObsluzeni;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if (obsluzeni_do_tej_pory >= maxObs) {
            printf("[Dyspozytor] Osiągnięto maks. liczbę obsłużonych pasażerów. Nie generuję nowego kapitana.\n");
            break; 
        }

        pid_t kap = fork();
        if(kap == 0) {
            char idxBuf[8];
            snprintf(idxBuf, sizeof(idxBuf), "%d", samolot_idx);
            execl("./kapitan_lotu", "kapitan_lotu", idxBuf, NULL);
            perror("[Dyspozytor] execl kapitan_lotu");
            exit(1);
        }

        semafor_op(semid, SEM_IDX_MUTEX, -1);
        wspolne->samoloty[samolot_idx].kapitan_pid = kap;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        printf("[Dyspozytor] URUCHAMIAM lot => samolot %d, kapitanPID=%d, Md=%d\n",
               samolot_idx, kap, wspolne->samoloty[samolot_idx].md);

        /* Odczekanie (t1 - t2) – po staremu, bez semtimedop */
        for(int s=0; s < (t1 - t2); s++){
            if(stopGen) break;
           sleep(1);
        }
        if(stopGen) break;

        /*
         * Otwieramy gate i jednocześnie "resetujemy" pojemność (SEM_IDX_POJEMNOSC)
         * bo ten nowy samolot też ma max P_VALUE (np. 15) miejsc.
         */
        semafor_op(semid, SEM_IDX_MUTEX, -1);

        // Ustawiamy semafor z powrotem na p (15)
        inicjuj_semafor(semid, SEM_IDX_POJEMNOSC, p);

        wspolne->gate.open = true;
        wspolne->gate_closed_for_this_plane = false;

        printf(YELLOW "[Dyspozytor]" RESET " OTWIERAM gate (samolot %d)\n", samolot_idx);
              
        int liczba_oczekujacych = semctl(semid, SEM_IDX_GATE_CHANGE, GETNCNT);
        if (liczba_oczekujacych == -1) {
            perror("[Dyspozytor] semctl(GETNCNT)");
            liczba_oczekujacych = 0;
        }
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        /* Wybudzenie wszystkich procesów czekających na SEM_IDX_GATE_CHANGE */
        if (liczba_oczekujacych > 0) {
            semafor_op(semid, SEM_IDX_GATE_CHANGE, liczba_oczekujacych);
        }

        /* Wybudzenie pasażerów czekających na SEM_IDX_VIP_WAIT (m.in. przeniesionych VIP) */
        int il = P_VALUE; // Tyle "slotów" w samolocie
        for (int i = 0; i < il; i++) {
            semafor_op(semid, SEM_IDX_VIP_WAIT, 1);
        }

        /* 
         * Zamiast pętli sleep(...) czekającej t2_minus_2,
         * używamy semtimedop na SEM_IDX_POJEMNOSC, czekając aż stanie się 0 (pełny)
         * albo minie czas t2_minus_2.
         */
        int t2_minus_2 = t2 - 2;
        if(t2_minus_2 < 0) t2_minus_2 = 0;

        // Przygotowanie struktury do semtimedop
        struct sembuf sops[1];
        sops[0].sem_num = SEM_IDX_POJEMNOSC; // czekamy, aż semafor == 0
        sops[0].sem_op  = 0;
        sops[0].sem_flg = 0;

        struct timespec tm;
        tm.tv_sec  = t2_minus_2; 
        tm.tv_nsec = 0;
        int ret;
        while(true) {
            ret = semtimedop(semid, sops, 1, &tm);
            if (ret == 0) {
                // Samolot wypełniony
                printf("[Dyspozytor] Samolot %d wyczerpał pojemność => zamykam gate.\n", samolot_idx);
                break;
            }
            if(ret == -1) {
                if(errno == EAGAIN) {
                    // Timeout
                    printf("[Dyspozytor] Upłynął czas => zamykam gate.\n");
                    break;
                }
                else if(errno == EINTR) {
                    // przerwane sygnałem
                    if(stopGen) {
                        // To SIGINT => kończymy
                        printf("[Dyspozytor] semtimedop: przerwano SIGINT => kończymy.\n");
                        break;
                    }
                    // To np. SIGCHLD => powtarzamy semtimedop
                    continue;
                }
                else {
                    // inny błąd
                    perror("[Dyspozytor] semtimedop");
                    break;
                }
            }
        }
        

        // Obojętnie czy ret=0 czy timeout czy EINTR, zamykamy gate (o ile nie przerwano pętli wyżej)
        if(!stopGen) {
            semafor_op(semid, SEM_IDX_MUTEX, -1);
            wspolne->gate.open = false;
            wspolne->gate_closed_for_this_plane = true;
            printf(YELLOW "[Dyspozytor]" RESET " ZAMYKAM gate (samolot %d)\n", samolot_idx);
            semafor_op(semid, SEM_IDX_MUTEX, 1);

            // Wybudzenie czekających na SEM_IDX_GATE_CHANGE
            if (liczba_oczekujacych > 0) {
                semafor_op(semid, SEM_IDX_GATE_CHANGE, liczba_oczekujacych);
            }

            // Dodatkowe 2 sekundy (opcjonalne odczekanie)
            for(int s=0; s<2; s++){
                if(stopGen) break;
            //     sleep(1);
            }
            if(stopGen) break;

            /* Informujemy kapitana, że może startować */
            semafor_op(semid, SEM_IDX_MUTEX, -1);
            pid_t kap_pid = wspolne->samoloty[samolot_idx].kapitan_pid;
            semafor_op(semid, SEM_IDX_MUTEX, 1);

            if(kap_pid > 0) {
                kill(kap_pid, SIGUSR1);
            }

            /* Czekamy na sygnał z kapitana (SEM_IDX_TAKEOFF_DONE) */
            if(!stopGen){
                semafor_op(semid, SEM_IDX_TAKEOFF_DONE, -1);
            }
        }
    } // koniec while(!stopGen)

    /* Kończymy generowanie pasażerów i czekamy na potomnych */
    semafor_op(semid, SEM_IDX_MUTEX, -1);
    wspolne->stop_odprawa = 1;
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    kill(gen_pid, SIGINT);
    waitpid(gen_pid, NULL, 0);

    printf("[Dyspozytor] Czekam na zakończenie wszystkich procesów potomnych...\n");
    while(wait(NULL) > 0) { }

    /* Usuwamy kolejkę komunikatów VIP */
    if(msgctl(vip_qid, IPC_RMID, NULL) == -1) {
        perror("[Dyspozytor] msgctl");
    } else {
        printf("[Dyspozytor] Usunięto kolejkę VIP.\n");
    }

    /* Sprzątanie zasobów */
    odlacz_pamiec_dzielonej(wspolne);
    usun_pamiec_dzielona(shm_id);
    usun_semafory(semid);

    int reta = system("ipcs | grep 'loto*' >> testy/test.txt");
        if (reta == -1) {
            perror("system");
        } else {
            printf("Wynik grepa zapisany do test\n");
        }

    printf("[Dyspozytor] Zakończono.\n");

   
    return 0;
}
