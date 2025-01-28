#include "semafor.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

int utworz_pamiec_dzielona(key_t klucz, size_t rozmiar)
{
    int shm_id = shmget(klucz, rozmiar, IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Blad shmget");
        return -1;
    }
    return shm_id;
}
void* dolacz_pamiec_dzielona(int shm_id)
{
    void *addr = shmat(shm_id, NULL, 0);
    if (addr == (void*)-1) {
        perror("Blad shmat");
        return (void*)-1;
    }
    return addr;
}
void odlacz_pamiec_dzielonej(const void *addr)
{
    if (shmdt(addr) == -1) {
        perror("Blad shmdt");
    }
}
void usun_pamiec_dzielona(int shm_id)
{
    if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
        perror("Blad shmctl(IPC_RMID)");
    }
}

int utworz_semafory(key_t klucz, int liczba)
{
    int semid = semget(klucz, liczba, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("Blad semget");
        return -1;
    }
    return semid;
}
void inicjuj_semafor(int semid, int semnum, int wartosc)
{
    union semun arg;
    arg.val = wartosc;
    if (semctl(semid, semnum, SETVAL, arg) == -1) {
        perror("Blad semctl(SETVAL)");
    }
}
void semafor_op(int semid, int semnum, int operacja)
{
    struct sembuf sb;
    sb.sem_num = semnum;
    sb.sem_op  = operacja;
    sb.sem_flg = 0;

    int retval;
    do {
        retval = semop(semid, &sb, 1);
        if(retval == -1 && errno != EINTR) {
            perror("Blad semop");
            break;  
        }
    } while(retval == -1 && errno == EINTR);
}
void usun_semafory(int semid)
{
    if (semctl(semid, 0, IPC_RMID) == -1) {
        perror("Blad semctl(IPC_RMID)");
    }
}



int semafor_op_ret(int semid, int semnum, int operacja, int flags) {
    struct sembuf sb;
    sb.sem_num = semnum;
    sb.sem_op  = operacja;
    sb.sem_flg = flags;  

    int retval;
    do {
        retval = semop(semid, &sb, 1);
        if(retval == -1 && errno != EINTR) {
            perror("Błąd semop w semafor_op_ret");
            break;
        }
    } while(retval == -1 && errno == EINTR);

    return retval;
}