#define _POSIX_C_SOURCE 200809L
// makro dla SA_RESTART | SA_NOCLDSTOP

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

static volatile sig_atomic_t stopGen = 0; // sygnał zatrzymania generatora pasażerów

static void handler_int(int s) {
    stopGen = 1;
    fprintf(stderr, "[Dyspozytor] Otrzymano SIGINT => przerwanie generowania pasażerów.\n");
}

static void sigchld_handler(int s) {
    // Sprzątanie po procesach-zombie
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

int main() {
    // Obsługa SIGCHLD
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("[Dyspozytor] sigaction(SIGCHLD)");
        exit(1);
    }

    // Obsługa SIGINT
    signal(SIGINT, handler_int);

    srand(time(NULL));

    /* Tworzymy i dołączamy pamięć dzieloną */
    int shm_id = utworz_pamiec_dzielona(KLUCZ_PAMIEC, sizeof(DaneWspolne));
    if (shm_id < 0) {
        perror("[Dyspozytor] shmget");
        exit(1);
    }
    DaneWspolne *wspolne = (DaneWspolne*)dolacz_pamiec_dzielona(shm_id);
    if ((void*)wspolne == (void*)-1) {
        perror("[Dyspozytor] shmat");
        exit(1);
    }

    /* Tworzymy/uzyskujemy tablicę semaforów */
    int sem_count = SEM_COUNT; 
    int semid = utworz_semafory(KLUCZ_SEMAFORY, sem_count);
    if (semid < 0) {
        perror("[Dyspozytor] semget");
        exit(1);
    }

    /* Tworzymy kolejkę komunikatów VIP */
    int vip_qid = msgget(VIP_QUEUE_KEY, IPC_CREAT | 0666);
    if (vip_qid == -1) {
        perror("[Dyspozytor] msgget VIP queue");
        exit(1);
    }

    // --- Inicjalizacja wszystkich semaforów ---
    inicjuj_semafor(semid, SEM_IDX_MUTEX, 1);

    // Stanowiska kontroli (5 stanowisk + semafor wolnych stanowisk)
    for (int i = 0; i < LICZBA_STANOWISK; i++) {
        inicjuj_semafor(semid, SEM_IDX_STANOWISKA + i, MAX_OSOBY_STANOWISKO);
    }
    inicjuj_semafor(semid, SEM_IDX_STANOWISKA_FREE, LICZBA_STANOWISK);

    // Pojemność samolotu (liczba wolnych miejsc)
    inicjuj_semafor(semid, SEM_IDX_POJEMNOSC, P_VALUE);

    // Liczba dostępnych miejsc na schodach
    inicjuj_semafor(semid, SEM_IDX_SCHODY, K_VALUE);

    // Różne semafory sygnalizacyjne
    inicjuj_semafor(semid, SEM_IDX_VIP_WAIT, 0);
    inicjuj_semafor(semid, SEM_IDX_START_EARLIER, 0);
    inicjuj_semafor(semid, SEM_IDX_SCHODY_OK, 0);
    inicjuj_semafor(semid, SEM_IDX_PLANE_AVAILABLE, 0);
    inicjuj_semafor(semid, SEM_IDX_TAKEOFF_DONE, 0);
    inicjuj_semafor(semid, SEM_IDX_Q_KONTROLA_CHANGED, 0);
    inicjuj_semafor(semid, SEM_IDX_Q_KONTROLA_SPACE, ROZMIAR_KOLEJKI);
    inicjuj_semafor(semid, SEM_IDX_GATE_CHANGE, 0);

    // --- Ustawienia w pamięci wspólnej ---
    semafor_op(semid, SEM_IDX_MUTEX, -1);

    wspolne->stop_odprawa = 0;
    wspolne->aktualne_Md  = 12; // bieżący limit bagażu w danym samolocie

    int p  = P_VALUE;  // max pojemność jednego samolotu (np. 15)
    int k  = K_VALUE;  // pojemność schodów
    int t1 = T1_VALUE; // 20 (maks. czas boardingu)
    int t2 = T2_VALUE; // 10 (najkrótszy czas, po którym możemy zakończyć boarding)
    int n  = N_VALUE;  // 4 samoloty

    wspolne->samoloty_skonczone = 0;
    wspolne->stan_samolotu = SAMOLOT_OCZEKUJE;
    wspolne->liczba_pasazerow_na_schodach = 0;

    for (int i = 0; i < LICZBA_STANOWISK; i++) {
        wspolne->stanowiska[i].ile_osob = 0;
        wspolne->stanowiska[i].plec_obecna = -1;
    }

    resetKolejka(&wspolne->kolejka_kontrola);
    resetGate(&wspolne->gate);

    int maxObs = MAX_PASSENGERS; // np. 100
    wspolne->pasazerowieObsluzeni = 0;

    // Md (limit wagi bagażu) dla 4 samolotów
    int md_values[N_VALUE] = {10, 11, 12, 11};
    for (int i = 0; i < N_VALUE; i++) {
        wspolne->md_for_plane[i] = md_values[i];
    }

    for (int i = 0; i < n; i++) {
        wspolne->samoloty[i].md   = wspolne->md_for_plane[i];
        wspolne->samoloty[i].stan = SAMOLOT_OCZEKUJE;
        wspolne->samoloty[i].idx  = i;
        wspolne->samoloty[i].kapitan_pid = -1;
        wspolne->samoloty[i].liczba_pasazerow = 0;
        // Każdy samolot "dostępny" -> semafor PLANE_AVAILABLE +1
        semafor_op(semid, SEM_IDX_PLANE_AVAILABLE, 1);
    }

    wspolne->samolot_ktory_wystartowal = -1;
    wspolne->gate_closed_for_this_plane = false;

    // Obliczamy max limit bagażu spośród wszystkich samolotów
    int max_limit = 0;
    for (int i = 0; i < n; i++) {
        if (wspolne->md_for_plane[i] > max_limit) {
            max_limit = wspolne->md_for_plane[i];
        }
    }
    wspolne->max_bagaz_limit = max_limit;
    wspolne->aktualny_samolot_idx = -1;

    semafor_op(semid, SEM_IDX_MUTEX, 1);

    printf("[Dyspozytor] Parametry: P=%d, K=%d, T1=%d, T2=%d, N=%d.\n", p, k, t1, t2, n);
    printf("[Dyspozytor] bagaż max=[10,12,12,11], max_bagaz_limit=%d\n", max_limit);

    /* --- Uruchamiamy proces generujący pasażerów --- */
    pid_t gen_pid = fork();
    if (gen_pid == 0) {
        // Kod potomny: generator pasażerów
        int maxPasazerow = maxObs; 
        int wygenerowani = 0;

        while (!stopGen && wygenerowani < maxPasazerow) {
            // Rezerwujemy miejsce w kolejce kontroli (nieblokująco)
            if (semafor_op_ret(semid, SEM_IDX_Q_KONTROLA_SPACE, -1) == -1) {
                if (errno == EAGAIN || errno == EINTR) {
                    // Kolejka pełna - odrzucamy pasażera
                    printf("[Dyspozytor Generujący] Kolejka kontroli pełna. Pasażer odrzucony.\n");

                    semafor_op(semid, SEM_IDX_MUTEX, -1);
                    wspolne->pasazerowieObsluzeni++;
                    semafor_op(semid, SEM_IDX_MUTEX, 1);

                    if (stopGen) break;
                    continue;
                } else {
                    perror("[Dyspozytor Generujący] SEM_IDX_Q_KONTROLA_SPACE");
                    break;
                }
            }

            if (stopGen) break;

            /* Losowanie parametrów pasażera */
            int plec = rand() % 2;            // 0..1
            bool danger = (rand() % 10 < 2);  // 20% szans
            bool vip = (rand() % 10 < 3);     // 30% szans

            char a1[8], a2[8], a3[8];
            snprintf(a1, sizeof(a1), "%d", plec);
            snprintf(a2, sizeof(a2), "%d", (danger ? 1 : 0));
            snprintf(a3, sizeof(a3), "%d", (vip ? 1 : 0));

            pid_t px = fork();
            if (px == 0) {
                //  uruchamiamy pasażera
                execl("./pasazer", "pasazer", a1, a2, a3, NULL);
                perror("[Dyspozytor Generujący] execl pasazer");
                exit(1);
            }

            wygenerowani++;
            sleep(1);
        }
        exit(0);
    }

    /* --- Główna pętla Dyspozytora (obsługa kolejnych samolotów) --- */
    while (!stopGen) {
        // Sprawdzamy, ilu pasażerów obsłużono
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int served = wspolne->pasazerowieObsluzeni;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if (served >= maxObs) {
            printf("[Dyspozytor] Osiągnięto limit %d obsłużonych pasażerów.\n", maxObs);
            stopGen = 1;
        }
        if (stopGen) break;

        // Czekamy na dostępny samolot (PLANE_AVAILABLE)
        semafor_op(semid, SEM_IDX_PLANE_AVAILABLE, -1);
        if (stopGen) break;

        // Sprawdzamy, które samoloty są dostępne
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int dostepne = -1;
        int dostepne_count = 0;
        int dostepne_indices[N_VALUE];

        for (int i = 0; i < n; i++) {
            if (wspolne->samoloty[i].stan == SAMOLOT_OCZEKUJE 
                || wspolne->samoloty[i].stan == SAMOLOT_WROCIL) {
                dostepne_indices[dostepne_count++] = i;
            }
        }
        // Losowy wybór z dostępnych samolotów
        if (dostepne_count > 0) {
            int random_idx = rand() % dostepne_count;
            dostepne = dostepne_indices[random_idx];
        }
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if (dostepne < 0) {
            // Fałszywe wybudzenie
            continue;
        }

        // Przygotowujemy ten samolot do lotu
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

        // Sprawdzamy, czy nie przekroczyliśmy już limitu pasażerów
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        int obsluzeni_do_tej_pory = wspolne->pasazerowieObsluzeni;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if (obsluzeni_do_tej_pory >= maxObs) {
            printf("[Dyspozytor] Osiągnięto maks. liczbę %d pasażerów. Nie uruchamiam nowego kapitana.\n", maxObs);
            break;
        }

        // Uruchamiamy proces kapitana
        pid_t kap = fork();
        if (kap == 0) {
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

        // Czekamy (t1 - t2) sekund (np. 20 - 10 = 10) zanim faktycznie otworzymy gate
        for (int s = 0; s < (t1 - t2); s++) {
            if (stopGen) break;
           sleep(1);
        }
        if (stopGen) break;

        // Otwieramy gate i "resetujemy" pojemność (bo nowy lot = nowy samolot)
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        // SEM_IDX_POJEMNOSC ustawiamy na p (np. 15)
        semctl(semid, SEM_IDX_POJEMNOSC, SETVAL, p);

        wspolne->gate.open = true;
        wspolne->gate_closed_for_this_plane = false;
        printf(YELLOW "[Dyspozytor]" RESET " OTWIERAM gate (samolot %d)\n", samolot_idx);

        // Wybudzenie oczekujących na SEM_IDX_GATE_CHANGE (nie wiemy dokładnie ilu, więc GETNCNT)
        int liczba_oczekujacych = semctl(semid, SEM_IDX_GATE_CHANGE, GETNCNT);
        if (liczba_oczekujacych == -1) {
            perror("[Dyspozytor] semctl(GETNCNT)");
            liczba_oczekujacych = 0;
        }

        semafor_op(semid, SEM_IDX_MUTEX, 1);
        if (liczba_oczekujacych > 0) {
            semafor_op(semid, SEM_IDX_GATE_CHANGE, liczba_oczekujacych);
        }

        // Wybudzamy ewentualnych VIP-ów czekających na kolejny lot
        for (int i = 0; i < p; i++) {
            semafor_op(semid, SEM_IDX_VIP_WAIT, 1);
        }

        // Oczekujemy, aż semafor POJEMNOSC dojdzie do 0 (samolot się wypełni)
        // lub upłynie czas (t2 - 2) => semtimedop
        int t2_minus_2 = t2 - 2;
        if (t2_minus_2 < 0) t2_minus_2 = 0;

        struct sembuf sops[1];
        sops[0].sem_num = SEM_IDX_POJEMNOSC;
        sops[0].sem_op  = 0;  // czekamy, aż wartość semafora będzie 0
        sops[0].sem_flg = 0;

        struct timespec tm;
        tm.tv_sec  = t2_minus_2;
        tm.tv_nsec = 0;

        while (true) {
            int ret = semtimedop(semid, sops, 1, &tm);
            if (ret == 0) {
                // Samolot wypełniony
                printf("[Dyspozytor] Samolot %d wyczerpał pojemność => zamykam gate.\n", samolot_idx);
                
                break;
            }
            if (ret == -1) {
                if (errno == EAGAIN) {
                    // Timeout
                    printf("[Dyspozytor] Upłynął czas => zamykam gate.\n");
                    break;
                } else if (errno == EINTR) {
                    // Przerwano sygnałem (SIGCHLD / SIGINT)
                    if (stopGen) {
                        printf("[Dyspozytor] semtimedop: przerwano SIGINT => kończymy.\n");
                        break;
                    }
                    // SIGCHLD => powtarzamy pętlę
                    continue;
                } else {
                    // inny błąd
                    perror("[Dyspozytor] semtimedop");
                    break;
                }
            }
        }

        // Zamykamy gate w jednej sekcji krytycznej => nikt więcej nie wejdzie
        if (!stopGen) {
            semafor_op(semid, SEM_IDX_MUTEX, -1);

            // Uniemożliwiamy pasażerom dalsze zdjęcie semafora POJEMNOSC
            semctl(semid, SEM_IDX_POJEMNOSC, SETVAL, 0);

            wspolne->gate.open = false;
            wspolne->gate_closed_for_this_plane = true;

            printf(YELLOW "[Dyspozytor]" RESET " ZAMYKAM gate (samolot %d)\n", samolot_idx);
            semafor_op(semid, SEM_IDX_MUTEX, 1);

            // Wybudzamy czekających na SEM_IDX_GATE_CHANGE
            if (liczba_oczekujacych > 0) {
                semafor_op(semid, SEM_IDX_GATE_CHANGE, liczba_oczekujacych);
            }

            // Opcjonalne 2 sekundy
            for (int s = 0; s < 2; s++) {
                if (stopGen) break;
                sleep(1);
            }
            if (stopGen) break;

            // Sygnalizujemy kapitanowi, że może startować (SIGUSR1)
            semafor_op(semid, SEM_IDX_MUTEX, -1);
            pid_t kap_pid = wspolne->samoloty[samolot_idx].kapitan_pid;
            semafor_op(semid, SEM_IDX_MUTEX, 1);

            if (kap_pid > 0) {
                kill(kap_pid, SIGUSR1);
            }

            // Czekamy, aż kapitan da sygnał, że wystartował i zakończył (SEM_IDX_TAKEOFF_DONE)
            if (!stopGen) {
                semafor_op(semid, SEM_IDX_TAKEOFF_DONE, -1);
            }
        }
    } // koniec while(!stopGen)

    // Kończymy generowanie pasażerów
    semafor_op(semid, SEM_IDX_MUTEX, -1);
    wspolne->stop_odprawa = 1;
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    kill(gen_pid, SIGINT);
    waitpid(gen_pid, NULL, 0);

    printf("[Dyspozytor] Czekam na zakończenie wszystkich procesów...\n");
    while (wait(NULL) > 0) {}

    // Usuwamy kolejkę VIP
    if (msgctl(vip_qid, IPC_RMID, NULL) == -1) {
        perror("[Dyspozytor] msgctl");
    } else {
        printf("[Dyspozytor] Usunięto kolejkę VIP.\n");
    }

    // Sprzątanie zasobów współdzielonych
    odlacz_pamiec_dzielonej(wspolne);
    usun_pamiec_dzielona(shm_id);
    usun_semafory(semid);

    // diagnostyka
    int reta = system("ipcs | grep 'loto*' >> testy/test.txt");
    if (reta == -1) {
        perror("system");
    } else {
        printf("Wynik grepa zapisany do testy/test.txt\n");
    }

    printf("[Dyspozytor] Zakończono.\n");
    return 0;
}
