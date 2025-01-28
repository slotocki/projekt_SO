#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

/* Konfiguracje */
#define LICZBA_STANOWISK      3
#define MAX_OSOBY_STANOWISKO  2
#define ROZMIAR_KOLEJKI       40
#define P_VALUE 15        
#define K_VALUE 5         
#define T1_VALUE 20       
#define T2_VALUE 10       
#define N_VALUE 4        
#define MAX_PASSENGERS 100 
#define SEM_COUNT 15 
#define KLUCZ_PAMIEC   0x1235
#define KLUCZ_SEMAFORY 0x2346
#define VIP_QUEUE_KEY  0x3456

/* Indeksy semaforów */
#define SEM_IDX_MUTEX           0  
#define SEM_IDX_STANOWISKA      1  
#define SEM_IDX_SCHODY          4    
#define SEM_IDX_POJEMNOSC       5
#define SEM_IDX_START_EARLIER   6
#define SEM_IDX_SCHODY_OK       7
#define SEM_IDX_PLANE_AVAILABLE 8
#define SEM_IDX_TAKEOFF_DONE    9
#define SEM_IDX_VIP_WAIT        10

#define SEM_IDX_Q_KONTROLA_SPACE 11
#define SEM_IDX_STANOWISKA_FREE 12
#define SEM_IDX_GATE_CHANGE 13
#define SEM_IDX_Q_KONTROLA_CHANGED 14


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

/* Element kolejki (pasażer) */
typedef struct {
    int  pid;
    Plec plec;
    bool vip;
    bool niebezpieczny;
    int  przepuszczenia;
    int  waga_bagazu;
} ElemKolejki;

/* Struktura kolejki */
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

/* Struktura opisująca samolot */
typedef struct {
    int md;                 /* limit bagażu dla tego samolotu */
    StanSamolotu stan;      
    int idx;                
    pid_t kapitan_pid;      
    int  liczba_pasazerow;  
} SamolotInfo;

/* Dane współdzielone między procesy */
typedef struct {
    int stop_odprawa;
    int aktualne_Md;
    int samoloty_skonczone;
    int pasazerowieObsluzeni;

    StanSamolotu stan_samolotu;
    int liczba_pasazerow_na_schodach;

    StanowiskoKontroli stanowiska[LICZBA_STANOWISK];

    Kolejka kolejka_kontrola;

    Gate gate;

    int md_for_plane[4];

    int samolot_ktory_wystartowal;
    bool gate_closed_for_this_plane;

    int max_bagaz_limit;

    
    SamolotInfo samoloty[4]; 

    int aktualny_samolot_idx;
} DaneWspolne;


typedef struct {
    long mtype;           
    ElemKolejki vip_elem; /* stan pasażera VIP */
} VipKolejkaKom;
typedef VipKolejkaKom WaitingQueueMsg;


void pushKolejka(Kolejka *k, ElemKolejki e);
ElemKolejki frontKolejka(const Kolejka *k);
ElemKolejki popKolejka(Kolejka *k);
void moveFrontToBack(Kolejka *k);
void resetKolejka(Kolejka *k);
void resetGate(Gate *g);
void wyswietl_kolejke(const char* nazwa, const Kolejka *k);

#endif