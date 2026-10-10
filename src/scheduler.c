/* Assignment 3: FCFS and preemptive SRTF treatment scheduling.
 * Selection functions are pure and apply deterministic arrival/severity/id ties.
 * The live tick loop synchronises one treatment unit with per-patient semaphores.
 * A separate pure simulator produces exact completion and waiting statistics. */
#include "common.h"
#include <errno.h>
#include <string.h>

int srtf_pick(const SchedProc p[], const int rem[], int n, int tick) {
    int best=-1;
    for(int i=0;i<n;i++) if(p[i].arrival<=tick && rem[i]>0 && (best<0 || rem[i]<rem[best] ||
       (rem[i]==rem[best] && (p[i].severity>p[best].severity || (p[i].severity==p[best].severity && p[i].id<p[best].id))))) best=i;
    return best;
}
int fcfs_pick(const SchedProc p[], const int rem[], int n, int tick, int current) {
    if(current>=0 && current<n && rem[current]>0 && p[current].arrival<=tick) return current;
    int best=-1;
    for(int i=0;i<n;i++) if(p[i].arrival<=tick && rem[i]>0 && (best<0 || p[i].arrival<p[best].arrival ||
       (p[i].arrival==p[best].arrival && p[i].id<p[best].id))) best=i;
    return best;
}
int sched_simulate(const SchedProc p[], int n, Policy policy, SchedResult *out) {
    if(!p||!out||n<1||n>MAX_PATIENTS) return -1;
    memset(out,0,sizeof *out); int rem[MAX_PATIENTS],done=0,tick=0,current=-1;
    for(int i=0;i<n;i++) rem[i]=p[i].burst;
    while(done<n){int pick=policy==POLICY_SRTF?srtf_pick(p,rem,n,tick):fcfs_pick(p,rem,n,tick,current);if(pick<0){tick++;current=-1;continue;} current=pick;rem[pick]--;tick++;if(!rem[pick]){out->completion[pick]=tick;done++;current=-1;}}
    for(int i=0;i<n;i++){out->turnaround[i]=out->completion[i]-p[i].arrival;out->waiting[i]=out->turnaround[i]-p[i].burst;out->avg_turnaround+=out->turnaround[i];out->avg_waiting+=out->waiting[i];}
    out->avg_turnaround/=n;out->avg_waiting/=n;return 0;
}

void scheduler_process(void) {
    int previous=-1, stuck=0;
    while(!g_shm->sim_done){
        int due_not_started=0;
        for(int i=0;i<g_shm->patient_count;i++)
            if(g_shm->p[i].state==EMPTY&&g_shm->p[i].arrival<=g_shm->tick)due_not_started=1;
        if(due_not_started){sleep_ms(2);continue;}
        int pick=-1;
        if(g_shm->policy==POLICY_FCFS && previous>=0 && g_shm->p[previous].state==READY) pick=previous;
        if(pick<0)for(int i=0;i<g_shm->patient_count;i++) if(g_shm->p[i].state==READY) {
            if(g_shm->policy==POLICY_FCFS){if(pick<0||g_shm->p[i].arrival<g_shm->p[pick].arrival||(g_shm->p[i].arrival==g_shm->p[pick].arrival&&g_shm->p[i].id<g_shm->p[pick].id))pick=i;}
            else if(pick<0||g_shm->p[i].remaining<g_shm->p[pick].remaining||(g_shm->p[i].remaining==g_shm->p[pick].remaining&&(g_shm->p[i].severity>g_shm->p[pick].severity||(g_shm->p[i].severity==g_shm->p[pick].severity&&g_shm->p[i].id<g_shm->p[pick].id))))pick=i;
        }
        if(pick>=0){
            if(previous>=0&&previous!=pick&&g_shm->p[previous].remaining>0) event_log("PREEMPT",g_shm->p[previous].id,"by=%d",g_shm->p[pick].id);
            g_shm->running_id=g_shm->p[pick].id;
            if(sem_post(&g_shm->p[pick].go)<0||sem_wait(&g_shm->p[pick].done)<0){event_log("ERROR",g_shm->p[pick].id,"scheduler semaphore: %s",strerror(errno));g_shm->internal_error=1;break;}
            if(g_shm->gantt_len<MAX_GANTT)g_shm->gantt[g_shm->gantt_len++]=(char)(g_shm->p[pick].id<10?'0'+g_shm->p[pick].id:'A'+g_shm->p[pick].id-10);
            previous=g_shm->p[pick].state==DONE?-1:pick;stuck=0;
        } else {
            g_shm->running_id=-1;event_log("IDLE",-1,"");if(g_shm->gantt_len<MAX_GANTT)g_shm->gantt[g_shm->gantt_len++]='.';
            int pending=0,waiting=0;for(int i=0;i<g_shm->patient_count;i++){if(g_shm->p[i].state==EMPTY)pending=1;if(g_shm->p[i].state==WAIT_RES)waiting=1;}
            if(!pending&&waiting&&++stuck>=50){event_log("ERROR",-1,"stuck WAIT_RES");g_shm->internal_error=1;break;} sleep_ms(g_shm->tick_ms);
        }
        g_shm->tick++;
        int done=0;for(int i=0;i<g_shm->patient_count;i++)if(g_shm->p[i].state==DONE)done++;
        if(done==g_shm->patient_count)break;
        if(pick>=0)sleep_ms(g_shm->tick_ms);
    }
    g_shm->running_id=-1;g_shm->sim_done=1;
}
