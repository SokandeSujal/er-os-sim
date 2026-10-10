/* Assignment 6: first readers-writers solution using process-shared semaphores.
 * Doctors share reads while nurses take exclusive access for multi-field writes.
 * The no-sync mode deliberately exposes partially updated records to readers.
 * Every doctor read also supplies the common FIFO/LRU cache reference stream. */
#define _DEFAULT_SOURCE
#include "common.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static void pause_random(unsigned *seed,int lo,int hi){sleep_ms(lo+(int)(rand_r(seed)%(unsigned)(hi-lo+1)));}
void doctor_process(int index){
    unsigned seed=(unsigned)g_shm->seed+1009U*(unsigned)(index+1);
    while(!g_shm->sim_done){int slot=cache_next_shared();int readers=0;
        if(!g_shm->no_sync){if(sem_wait(&g_shm->rw_mutex)<0)_exit(2);g_shm->read_count++;readers=g_shm->read_count;if(g_shm->read_count==1&&sem_wait(&g_shm->rw_wrt)<0)_exit(2);sem_post(&g_shm->rw_mutex);}else{g_shm->read_count++;readers=g_shm->read_count;}
        event_log("READ_START",g_shm->p[slot].id,"readers=%d",readers);usleep(20000);int a=g_shm->rec[slot].a,b=g_shm->rec[slot].b,sum=g_shm->rec[slot].sum;if(a+b!=sum)__sync_fetch_and_add(&g_shm->corrupt,1);__sync_fetch_and_add(&g_shm->total_reads,1);event_log("READ_END",g_shm->p[slot].id,"valid=%s",a+b==sum?"yes":"no");
        if(!g_shm->no_sync){sem_wait(&g_shm->rw_mutex);g_shm->read_count--;if(g_shm->read_count==0)sem_post(&g_shm->rw_wrt);sem_post(&g_shm->rw_mutex);}else g_shm->read_count--;pause_random(&seed,100,400);
    }_exit(0);
}
void nurse_process(int index){
    unsigned seed=(unsigned)g_shm->seed+7919U*(unsigned)(index+1);
    while(!g_shm->sim_done){int slot=(int)(rand_r(&seed)%(unsigned)g_shm->patient_count);if(!g_shm->no_sync&&sem_wait(&g_shm->rw_wrt)<0)_exit(2);g_shm->writer_active=1;event_log("WRITE_START",g_shm->p[slot].id,"");int v=g_shm->rec[slot].version+1;g_shm->rec[slot].a=(int)(rand_r(&seed)%100U);usleep(150000);g_shm->rec[slot].b=(int)(rand_r(&seed)%100U);g_shm->rec[slot].sum=g_shm->rec[slot].a+g_shm->rec[slot].b;g_shm->rec[slot].version=v;__sync_fetch_and_add(&g_shm->total_writes,1);event_log("WRITE_END",g_shm->p[slot].id,"version=%d",v);g_shm->writer_active=0;if(!g_shm->no_sync)sem_post(&g_shm->rw_wrt);pause_random(&seed,300,800);}
    _exit(0);
}
