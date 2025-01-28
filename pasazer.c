#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>   
#include "common.h"
#include "semafor.h"
#include "colors.h"

static volatile sig_atomic_t stopSig = 0;
static void sig_stop(int s) {
    stopSig = 1;
}

/* Funkcja wywoływana przed zakończeniem procesu pasażera */
void pasazerKoniec(DaneWspolne *wspolne, int semid) {
    semafor_op(semid, SEM_IDX_MUTEX, -1);
    wspolne->pasazerowieObsluzeni++;
    semafor_op(semid, SEM_IDX_MUTEX, 1);
}

/* 
   Pomocnicza funkcja do „binarnych” powiadomień o zmianie kolejki.
   Inkrementuje semafor SEM_IDX_Q_KONTROLA_CHANGED tylko wtedy, 
   gdy jego wartość wynosi 0 (czyli nikt nie oczekuje na wybudzenie).
*/
static void signalQueueChange(int semid) {
    int curr_val = semctl(semid, SEM_IDX_Q_KONTROLA_CHANGED, GETVAL, 0);
    if(curr_val == -1) {
        perror("[Pasażer] semctl(GETVAL)");
        return;
    }
    if(curr_val == 0) {
        semafor_op(semid, SEM_IDX_Q_KONTROLA_CHANGED, 1);
    }
}

int main(int argc, char *argv[]) {
    signal(SIGINT, sig_stop);
    srand(time(NULL) ^ getpid());

    int sem_count = SEM_COUNT; 

    int plec = 0, danger = 0, vip = 0;
    if(argc > 1) plec   = atoi(argv[1]);
    if(argc > 2) danger = atoi(argv[2]);
    if(argc > 3) vip    = atoi(argv[3]);

    bool czy_kobieta   = (plec == 1);
    bool is_vip        = (vip == 1);
    bool niebezpieczny = (danger == 1);

    // Obsługa pamięci wspólnej
    int shm_id = utworz_pamiec_dzielona(KLUCZ_PAMIEC, sizeof(DaneWspolne));
    if(shm_id < 0) {
        perror("[Pasażer] shmget");
        exit(1);
    }
    DaneWspolne *wspolne = (DaneWspolne*) dolacz_pamiec_dzielona(shm_id);
    if((void*)wspolne == (void*)-1) {
        perror("[Pasażer] shmat");
        exit(1);
    }

    // Obsługa semaforów
    int semid = utworz_semafory(KLUCZ_SEMAFORY, sem_count);
    if(semid < 0) {
        perror("[Pasażer] semget");
        exit(1);
    }

    /* Dostęp do kolejki komunikatów (VIP + heavy) */
    int vip_qid = msgget(VIP_QUEUE_KEY, IPC_CREAT | 0666);
    if(vip_qid == -1) {
        perror("[Pasażer] msgget VIP queue");
        pasazerKoniec(wspolne, semid);
        odlacz_pamiec_dzielonej(wspolne);
        exit(1);
    }

    semafor_op(semid, SEM_IDX_MUTEX, -1);
    int stop_flag = wspolne->stop_odprawa;
    int maxLimit  = wspolne->max_bagaz_limit;
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    if(stop_flag) {
        printf("[Pasażer %d] Odprawa zamknięta => rezygnuję.\n", getpid());
        pasazerKoniec(wspolne, semid);
        odlacz_pamiec_dzielonej(wspolne);
        return 0;
    }

    // Losujemy wagę bagażu 6..12
    int waga_bagazu = 7 + (rand() % 7);
    printf("[Pasażer %d] płeć=%s, VIP=%s, niebezp=%s, waga=%d\n",
           getpid(),
           (czy_kobieta ? "Kobieta" : "Mężczyzna"),
           (is_vip ? "TAK" : "NIE"),
           (niebezpieczny ? "TAK" : "NIE"),
           waga_bagazu);

    if(waga_bagazu > maxLimit) {
        printf(RED "[Pasażer %d]" RESET " Bagaż=%d > globalnyLimit=%d => odrzucony.\n",
               getpid(), waga_bagazu, maxLimit);
        pasazerKoniec(wspolne, semid);
        odlacz_pamiec_dzielonej(wspolne);
        return 0;
    }
    printf(GREEN "[Pasażer %d]" RESET " Odprawa OK (bagaż <= %d)\n", getpid(), maxLimit);

    // Tworzymy obiekt do kolejki
    ElemKolejki e;
    e.pid            = getpid();
    e.plec           = (czy_kobieta ? PLEC_KOBIETA : PLEC_MEZCZYZNA);
    e.vip            = is_vip;
    e.niebezpieczny  = niebezpieczny;
    e.przepuszczenia = 0;
    e.waga_bagazu    = waga_bagazu;

    /* Dołączamy do kolejki kontroli bezpieczeństwa */
    semafor_op(semid, SEM_IDX_MUTEX, -1);
    if(wspolne->kolejka_kontrola.liczba >= ROZMIAR_KOLEJKI) {
        printf("[Pasażer %d] Kolejka kontroli pełna. Kończę proces.\n", getpid());
        semafor_op(semid, SEM_IDX_MUTEX, 1);
        pasazerKoniec(wspolne, semid);
        odlacz_pamiec_dzielonej(wspolne);
        return 0;
    }

    pushKolejka(&wspolne->kolejka_kontrola, e);
    printf("[Pasażer %d] Dołączam do kolejki kontroli bezpieczeństwa.\n", getpid());
   

    signalQueueChange(semid); 
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    /* Oczekiwanie w kolejce do kontroli bezpieczeństwa */
    bool naPrzodzie = true;
    while(!stopSig && naPrzodzie) {
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        if(wspolne->kolejka_kontrola.liczba > 0) {
            ElemKolejki f = frontKolejka(&wspolne->kolejka_kontrola);
            if(f.pid == getpid()) {
                // Pasażer jest na przodzie
                bool stanowiskoDostepne = false;
                for(int i = 0; i < LICZBA_STANOWISK; i++) {
                    int ile = wspolne->stanowiska[i].ile_osob;
                    Plec pl = wspolne->stanowiska[i].plec_obecna;
                    if(ile < MAX_OSOBY_STANOWISKO && (ile == 0 || pl == e.plec)) {
                        stanowiskoDostepne = true;
                        break;
                    }
                }
                if(stanowiskoDostepne) {
                    popKolejka(&wspolne->kolejka_kontrola);
                    signalQueueChange(semid);  
                    semafor_op(semid, SEM_IDX_Q_KONTROLA_SPACE, 1); 
                    semafor_op(semid, SEM_IDX_MUTEX, 1);
                    naPrzodzie = false;
                } else {
                    semafor_op(semid, SEM_IDX_MUTEX, 1);
                }
            } else {
                // Próba przepuszczenia kogoś innego
                if(rand() % 100 < 10) {
                    e.przepuszczenia++;
                    if(e.przepuszczenia > 3) {
                        e.niebezpieczny = true;
                    }
                    printf("[Pasażer %d] Przepuszczam osobę %d. (Przepuszczeń=%d, niebezpieczny=%s)\n",
                           getpid(), f.pid, e.przepuszczenia,
                           (e.niebezpieczny ? "TAK" : "NIE"));
                    moveFrontToBack(&wspolne->kolejka_kontrola);
                    signalQueueChange(semid);
                }
                semafor_op(semid, SEM_IDX_MUTEX, 1);
            }
        } else {
            semafor_op(semid, SEM_IDX_MUTEX, 1);
        }

        if(naPrzodzie) {
            semafor_op(semid, SEM_IDX_Q_KONTROLA_CHANGED, -1);
        }
    }
    if(stopSig) {
        pasazerKoniec(wspolne, semid);
        odlacz_pamiec_dzielonej(wspolne);
        return 0;
    }

    /* Szukamy wolnego stanowiska w kontroli bezpieczeństwa */
    bool wKontroli = false;
    while(!stopSig && !wKontroli) {
        // Czekanie na dostępne stanowisko
        semafor_op(semid, SEM_IDX_STANOWISKA_FREE, -1);

        // Sekcja krytyczna: Szukanie wolnego stanowiska
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        for (int i = 0; i < LICZBA_STANOWISK; i++) {
            int ile = wspolne->stanowiska[i].ile_osob;
            Plec pl = wspolne->stanowiska[i].plec_obecna;
            if(ile < MAX_OSOBY_STANOWISKO && (ile == 0 || pl == e.plec)) {
                // rezerwujemy 1 wolne miejsce
                semafor_op(semid, SEM_IDX_STANOWISKA + i, -1);
                wspolne->stanowiska[i].ile_osob++;
                if(wspolne->stanowiska[i].ile_osob == 1) {
                    wspolne->stanowiska[i].plec_obecna = e.plec;
                }
                printf("[Pasażer %d] Wchodzę na stanowisko kontroli %d (aktualnie %d osób tam).\n",
                       getpid(), i, wspolne->stanowiska[i].ile_osob);
                semafor_op(semid, SEM_IDX_MUTEX, 1);

                // Symulacja kontroli
                //sleep(2 + rand() % 3);

                if(e.niebezpieczny) {
                    printf("[Pasażer %d] Wykryto niebezpieczny przedmiot, odrzucony!\n", getpid());
                    // Zwolnienie stanowiska
                    semafor_op(semid, SEM_IDX_MUTEX, -1);
                    wspolne->stanowiska[i].ile_osob--;
                    if(wspolne->stanowiska[i].ile_osob == 0) {
                        wspolne->stanowiska[i].plec_obecna = -1;
                    }
                    semafor_op(semid, SEM_IDX_MUTEX, 1);

                    semafor_op(semid, SEM_IDX_STANOWISKA + i, 1);
                    semafor_op(semid, SEM_IDX_STANOWISKA_FREE, 1);
                    pasazerKoniec(wspolne, semid);
                    odlacz_pamiec_dzielonej(wspolne);
                    return 0;
                }

                printf(BLUE "[Pasażer %d]" RESET " Kończę kontrolę => stanowisko=%d\n",
                       getpid(), i);

                // Zwolnienie stanowiska
                semafor_op(semid, SEM_IDX_MUTEX, -1);
                wspolne->stanowiska[i].ile_osob--;
                if(wspolne->stanowiska[i].ile_osob == 0) {
                    wspolne->stanowiska[i].plec_obecna = -1;
                }
                semafor_op(semid, SEM_IDX_MUTEX, 1);

                printf("[Pasażer %d] Opuszczam kontrolę na stanowisku %d.\n", getpid(), i);
                semafor_op(semid, SEM_IDX_STANOWISKA + i, 1);
                semafor_op(semid, SEM_IDX_STANOWISKA_FREE, 1);

                wKontroli = true;
                break;
            }
        }
        if(!wKontroli) {
            // nie znaleziono wolnego stanowiska zgodnego z płcią
            semafor_op(semid, SEM_IDX_MUTEX, 1);
        }
    }
    if(stopSig) {
        printf("[Pasażer %d] STOP w trakcie kontroli.\n", getpid());
        pasazerKoniec(wspolne, semid);
        semafor_op(semid, SEM_IDX_STANOWISKA_FREE, 1);
        odlacz_pamiec_dzielonej(wspolne);
        return 0;
    }

    /* W hali odlotów => do bramki VIP lub normal */
    semafor_op(semid, SEM_IDX_MUTEX, -1);
    Kolejka *docelowa_kolejka = (e.vip ? &wspolne->gate.vip_queue : &wspolne->gate.normal_queue);
    if(docelowa_kolejka->liczba >= ROZMIAR_KOLEJKI) {
        printf("[Pasażer %d] Kolejka do bramki %s pełna. Kończę proces.\n",
               getpid(), (e.vip ? "VIP" : "normalna"));
        semafor_op(semid, SEM_IDX_MUTEX, 1);
        pasazerKoniec(wspolne, semid);
        odlacz_pamiec_dzielonej(wspolne);
        return 0;
    }
    pushKolejka(docelowa_kolejka, e);
    semafor_op(semid, SEM_IDX_MUTEX, 1);

    /* Oczekiwanie na otwarty gate i wchodzenie na pokład */
    while(!stopSig) {
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        bool open    = wspolne->gate.open;
        bool closed  = wspolne->gate_closed_for_this_plane;
        int current_md = wspolne->aktualne_Md;
        int aktualny_samolot_idx = wspolne->aktualny_samolot_idx;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if(!open || closed) {
            // gate zamknięty => czekamy
            semafor_op(semid, SEM_IDX_GATE_CHANGE, -1);
            continue;
        }

        /* Czy jesteśmy na przodzie właściwej kolejki? (VIP ma priorytet) */
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        bool im_front = false;
        if(e.vip) {
            if(wspolne->gate.vip_queue.liczba > 0) {
                ElemKolejki f_vip = frontKolejka(&wspolne->gate.vip_queue);
                if(f_vip.pid == getpid()) {
                    im_front = true;
                }
            }
        } else {
            // zwykły pasażer może wejść dopiero, gdy VIP kolejka jest pusta
            if(wspolne->gate.vip_queue.liczba == 0 && wspolne->gate.normal_queue.liczba > 0) {
                ElemKolejki f_nrm = frontKolejka(&wspolne->gate.normal_queue);
                if(f_nrm.pid == getpid()) {
                    im_front = true;
                }
            }
        }
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        if(!im_front) {
            // czekamy chwilę
            sleep(1);
            continue;
        }

        /* Sprawdzamy bieżący limit Md samolotu */
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        if(e.waga_bagazu > current_md) {
            printf(RED "[Pasażer %d] " RESET 
                   "Bagaż=%d > Md=%d => czekam na kolejny samolot (odkładam do kolejki komunikatów)\n",
                   getpid(), e.waga_bagazu, current_md);

            // Zdejmij z kolejki bramki
            if(e.vip) {
                popKolejka(&wspolne->gate.vip_queue);
            } else {
                popKolejka(&wspolne->gate.normal_queue);
            }

            // Wysyłamy do kolejki komunikatów System V
            WaitingQueueMsg msg;
            msg.mtype = 1;
            msg.vip_elem = e;
            if(msgsnd(vip_qid, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT) == -1) {
                printf("[Pasażer %d] Kolejka komunikatów pełna. Rezygnuję z wejścia.\n", getpid());
                semafor_op(semid, SEM_IDX_MUTEX, 1);
                pasazerKoniec(wspolne, semid);
                odlacz_pamiec_dzielonej(wspolne);
                return 0;
            }

            semafor_op(semid, SEM_IDX_MUTEX, 1);

            // Oczekiwanie na ponowne otwarcie gate => identycznie jak VIP
            semafor_op(semid, SEM_IDX_VIP_WAIT, -1);
            continue;
        }
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        /* Próbujemy (nieblokująco) zająć miejsce w samolocie + schodach */
        struct sembuf ops[2];
        ops[0].sem_num = SEM_IDX_POJEMNOSC; // wolne miejsca w samolocie
        ops[0].sem_op  = -1;
        ops[0].sem_flg = IPC_NOWAIT;

        ops[1].sem_num = SEM_IDX_SCHODY;    // wolne miejsce na schodach
        ops[1].sem_op  = -1;
        ops[1].sem_flg = IPC_NOWAIT;

        if(semop(semid, ops, 2) == -1) {
            if(errno == EAGAIN) {
                printf("[Pasażer %d] Samolot lub schody pełne => odkładam do kolejki komunikatów.\n",
                       getpid());
                // Zdejmij z kolejki bramki
                semafor_op(semid, SEM_IDX_MUTEX, -1);
                if(e.vip) {
                    popKolejka(&wspolne->gate.vip_queue);
                } else {
                    popKolejka(&wspolne->gate.normal_queue);
                }

                // Wysyłamy do kolejki komunikatów
                WaitingQueueMsg msg;
                msg.mtype = 1;
                msg.vip_elem = e;
                if(msgsnd(vip_qid, &msg, sizeof(msg) - sizeof(long), IPC_NOWAIT) == -1) {
                    printf("[Pasażer %d] Kolejka komunikatów pełna. Rezygnuję.\n", getpid());
                    semafor_op(semid, SEM_IDX_MUTEX, 1);
                    pasazerKoniec(wspolne, semid);
                    odlacz_pamiec_dzielonej(wspolne);
                    return 0;
                }

                semafor_op(semid, SEM_IDX_MUTEX, 1);

                // czekamy na kolejny samolot
                semafor_op(semid, SEM_IDX_VIP_WAIT, -1);
                continue;
            } else {
                perror("[Pasażer] semop IPC_NOWAIT");
                pasazerKoniec(wspolne, semid);
                odlacz_pamiec_dzielonej(wspolne);
                return 0;
            }
        }

        /* Sprawdzamy, czy gate nadal otwarty (mogło się zamknąć w międzyczasie) */
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        bool nadal_otwarte = (!wspolne->gate_closed_for_this_plane && wspolne->gate.open);
        if(!nadal_otwarte) {
            // Gate się zamknął => zwracamy semafory
            semafor_op(semid, SEM_IDX_POJEMNOSC, 1);
            semafor_op(semid, SEM_IDX_MUTEX, 1);
            continue;
        }
        // Zdejmujemy się z kolejki gate
        if(e.vip) {
            popKolejka(&wspolne->gate.vip_queue);
        } else {
            popKolejka(&wspolne->gate.normal_queue);
        }

        wspolne->liczba_pasazerow_na_schodach++;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        // "Wejście na schody"
        usleep(100);

        // Zdejmowanie pasażera ze schodów + dodanie do samolotu
        semafor_op(semid, SEM_IDX_MUTEX, -1);
        wspolne->liczba_pasazerow_na_schodach--;
        if(wspolne->liczba_pasazerow_na_schodach == 0) {
            semafor_op(semid, SEM_IDX_SCHODY_OK, 1);
        }
        wspolne->samoloty[aktualny_samolot_idx].liczba_pasazerow++;
        int inside = wspolne->samoloty[aktualny_samolot_idx].liczba_pasazerow;
        semafor_op(semid, SEM_IDX_MUTEX, 1);

        semafor_op(semid, SEM_IDX_SCHODY, 1);

        printf("[Pasażer %d] Wsiadam do samolotu, w samolocie=%d.\n", getpid(), inside);
        break; // dotarliśmy do samolotu => koniec pętli
    }

    pasazerKoniec(wspolne, semid);
    odlacz_pamiec_dzielonej(wspolne);
    return 0;
}
