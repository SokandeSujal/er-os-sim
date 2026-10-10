/* Assignments 2, 3, 5 and 6: one forked process models each patient.
 * Ventilators are acquired before Banker's resources to prevent hidden deadlock.
 * Per-patient go/done semaphores execute exactly one treatment unit per tick.
 * State changes precede done signals, so the scheduler never observes stale state. */
#define _DEFAULT_SOURCE
#include "common.h"
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

static int request_until_granted(int slot,const int req[]){
    int denied=0;
    for(;;){BankerResult r=banker_request(slot,req);if(r==GRANTED){event_log("BANKER_GRANT",g_shm->p[slot].id,"request=[%d,%d,%d]",req[0],req[1],req[2]);return 0;}if(r==ERROR_EXCEEDS_MAX){event_log("ERROR",g_shm->p[slot].id,"request exceeds maximum");return -1;}if(!denied){event_log("BANKER_DENY",g_shm->p[slot].id,"reason=%s",r==WAIT_UNSAFE?"unsafe":"unavailable");denied=1;}sleep_ms(50);}
}
void patient_process(int slot){
    signal(SIGINT,SIG_DFL);signal(SIGTERM,SIG_DFL);
    Patient *p=&g_shm->p[slot];p->pid=getpid();p->state=ARRIVED;event_log("ARRIVE",p->id,"burst=%d severity=%d",p->burst,p->severity);
    if(p->needs_vent){p->state=WAIT_VENT;event_log("VENT_WAIT",p->id,"");if(sem_wait(&g_shm->vent)<0){event_log("ERROR",p->id,"vent sem_wait: %s",strerror(errno));_exit(2);}p->has_vent=1;event_log("VENT_ACQ",p->id,"");}
    p->state=WAIT_RES;if(request_until_granted(slot,p->req1)<0)_exit(2);p->state=READY;
    while(p->remaining>0){
        if(sem_wait(&p->go)<0){event_log("ERROR",p->id,"go sem_wait: %s",strerror(errno));_exit(2);}p->state=RUNNING;if(p->start_tick<0)p->start_tick=g_shm->tick;p->remaining--;event_log("RUN",p->id,"remaining=%d",p->remaining);
        if(p->remaining==0){if(banker_release(slot)<0)event_log("ERROR",p->id,"banker release");if(p->has_vent){p->has_vent=0;if(sem_post(&g_shm->vent)<0)event_log("ERROR",p->id,"vent sem_post");else event_log("VENT_REL",p->id,"");}p->completion_tick=g_shm->tick+1;p->state=DONE;event_log("DONE",p->id,"CT=%d",p->completion_tick);if(sem_post(&p->done)<0)_exit(2);_exit(0);}
        int second=p->remaining==p->req2_at&&(p->req2[0]||p->req2[1]||p->req2[2]);
        if(second)p->state=WAIT_RES;else p->state=READY;
        if(sem_post(&p->done)<0)_exit(2);
        if(second){if(request_until_granted(slot,p->req2)<0)_exit(2);p->req2[0]=p->req2[1]=p->req2[2]=0;p->state=READY;}
    }
    _exit(0);
}
