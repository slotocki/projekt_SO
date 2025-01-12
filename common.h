#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>

/* Konfiguracje */
#define LICZBA_STANOWISK      3
#define MAX_OSOBY_STANOWISKO  2
#define ROZMIAR_KOLEJKI       50

#define KLUCZ_PAMIEC   0x1234
#define KLUCZ_SEMAFORY 0x2345

/* Indeksy semaforów */
#define SEM_IDX_MUTEX        0  
#define SEM_IDX_STANOWISKA   1  
#define SEM_IDX_SCHODY       (LICZBA_STANOWISK + SEM_IDX_STANOWISKA) 
#define SEM_IDX_POJEMNOSC    (LICZBA_STANOWISK + SEM_IDX_STANOWISKA + 1) 

/* Płeć pasażera */
typedef enum {
    PLEC_MEZCZYZNA=0,
    PLEC_KOBIETA
} Plec;

/* Stanowisko kontroli */
typedef struct {
    int  ile_osob;      
    Plec plec_obecna;  
} StanowiskoKontroli;

/* Stan samolotu */
typedef enum {
    SAMOLOT_OCZEKUJE=0,
    SAMOLOT_LECI,
    SAMOLOT_WROCIL
} StanSamolotu;

/* Element kolejki */
typedef struct {
    int  pid;
    Plec plec;
    bool vip;
    bool dangerous_item;
    int  przepuszczenia;  
    int  waga_bagazu;
} ElemKolejki;

/*  kolejka FIFO */
typedef struct {
    ElemKolejki tab[ROZMIAR_KOLEJKI];
    int pocz;
    int kon;
    int liczba;
} Kolejka;

/* Gate */
typedef struct {
    Kolejka vip_queue;
    Kolejka normal_queue;
    bool open; 
} Gate;

typedef struct {
    int stop_odprawa;            
    int aktualne_Md;            
    int P;                     
    int K;                      
    int T1;                     
    int N;                      
    int samoloty_skonczone;     

    StanSamolotu stan_samolotu;
    int liczba_pasazerow_w_samolocie;
    int liczba_pasazerow_na_schodach;

    StanowiskoKontroli stanowiska[LICZBA_STANOWISK];

    Kolejka kolejka_vip;
    Kolejka kolejka_normal;

    Gate gate;

    int md_for_plane[10];
    int current_plane_idx;

    bool gate_closed_for_this_plane;
} DaneWspolne;

void wyswietl_kolejke(const char* nazwa, const Kolejka *k);

#endif
