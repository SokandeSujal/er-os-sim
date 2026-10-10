/* Assignments 2-7: admission desk owns setup, process creation and cleanup.
 * It creates shared memory and the common pipe before forking any child.
 * Patients are forked at arrival ticks and all children are explicitly reaped.
 * Signal-driven cleanup terminates children and unlinks stale shared memory. */
#define _DEFAULT_SOURCE
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;
static void on_signal(int sig){(void)sig;stop_requested=1;}
static void usage(const char*n){printf("Usage: %s --scenario FILE [--policy srtf|fcfs] [--tick-ms N|--fast]\n       [--no-ui] [--no-sync] [--cache-size N] [--seed N] [--demo cache]\n",n);}
static int number(const char*s,int min,int max,int*out){char*e;long v=strtol(s,&e,10);if(!*s||*e||v<min||v>max)return-1;*out=(int)v;return 0;}
static int sem_make(sem_t*s,unsigned v){if(sem_init(s,1,v)<0){perror("sem_init");return-1;}return 0;}
static void cache_init(CacheState*s,int n){s->size=n;s->next=0;for(int i=0;i<MAX_PATIENTS;i++)s->frames[i]=-1;}

int main(int argc,char**argv){
    const char*scenario_path=NULL;int policy_override=-1,tick_override=-1,cache_override=-1,no_ui=0,no_sync=0,seed=42;
    for(int i=1;i<argc;i++){if(!strcmp(argv[i],"--help")){usage(argv[0]);return 0;}else if(!strcmp(argv[i],"--scenario")&&i+1<argc)scenario_path=argv[++i];else if(!strcmp(argv[i],"--policy")&&i+1<argc){char*v=argv[++i];if(!strcmp(v,"srtf"))policy_override=POLICY_SRTF;else if(!strcmp(v,"fcfs"))policy_override=POLICY_FCFS;else{fprintf(stderr,"invalid policy\n");return 1;}}else if(!strcmp(argv[i],"--tick-ms")&&i+1<argc){if(number(argv[++i],1,60000,&tick_override)){fprintf(stderr,"invalid tick-ms\n");return 1;}}else if(!strcmp(argv[i],"--fast"))tick_override=50;else if(!strcmp(argv[i],"--no-ui"))no_ui=1;else if(!strcmp(argv[i],"--no-sync"))no_sync=1;else if(!strcmp(argv[i],"--cache-size")&&i+1<argc){if(number(argv[++i],1,MAX_PATIENTS,&cache_override)){fprintf(stderr,"invalid cache size\n");return 1;}}else if(!strcmp(argv[i],"--seed")&&i+1<argc){if(number(argv[++i],0,1000000,&seed)){fprintf(stderr,"invalid seed\n");return 1;}}else if(!strcmp(argv[i],"--demo")&&i+1<argc&&!strcmp(argv[++i],"cache"))return cache_demo();else{fprintf(stderr,"unknown or incomplete option: %s\n",argv[i]);usage(argv[0]);return 1;}}
    if(!scenario_path){fprintf(stderr,"--scenario is required\n");return 1;}Scenario sc;char error[256];if(load_scenario(scenario_path,&sc,error,sizeof error)){fprintf(stderr,"Scenario error: %s\n",error);return 1;}if(policy_override>=0)sc.policy=policy_override;if(tick_override>0)sc.tick_ms=tick_override;if(cache_override>0)sc.cache_size=cache_override;
    shm_unlink(SHM_NAME);int shmfd=shm_open(SHM_NAME,O_CREAT|O_EXCL|O_RDWR,0600);if(shmfd<0){perror("shm_open");return 2;}if(ftruncate(shmfd,(off_t)sizeof(Shared))<0){perror("ftruncate");shm_unlink(SHM_NAME);return 2;}g_shm=mmap(NULL,sizeof(Shared),PROT_READ|PROT_WRITE,MAP_SHARED,shmfd,0);close(shmfd);if(g_shm==MAP_FAILED){perror("mmap");shm_unlink(SHM_NAME);return 2;}memset(g_shm,0,sizeof *g_shm);
    g_shm->policy=sc.policy;g_shm->tick_ms=sc.tick_ms;g_shm->no_sync=no_sync;g_shm->no_ui=no_ui;g_shm->seed=seed;g_shm->ventilators=sc.ventilators;g_shm->readers=sc.readers;g_shm->writers=sc.writers;g_shm->cache_size=sc.cache_size;g_shm->patient_count=sc.patient_count;g_shm->running_id=-1;for(int j=0;j<3;j++)g_shm->total[j]=g_shm->available[j]=sc.total[j];
    if(sem_make(&g_shm->banker_mutex,1)||sem_make(&g_shm->vent,(unsigned)sc.ventilators)||sem_make(&g_shm->rw_mutex,1)||sem_make(&g_shm->rw_wrt,1)||sem_make(&g_shm->cache_mutex,1)||sem_make(&g_shm->feed_mutex,1)){munmap(g_shm,sizeof *g_shm);shm_unlink(SHM_NAME);return 2;}
    cache_init(&g_shm->fifo,sc.cache_size);cache_init(&g_shm->lru,sc.cache_size);
    for(int i=0;i<sc.patient_count;i++){Patient*p=&g_shm->p[i];PatientSpec*x=&sc.patients[i];p->id=x->id;p->state=EMPTY;p->arrival=x->arrival;p->burst=p->remaining=x->burst;p->severity=x->severity;p->needs_vent=x->needs_vent;p->req2_at=x->req2_at;p->start_tick=-1;p->completion_tick=-1;for(int j=0;j<3;j++){p->max[j]=x->max[j];p->req1[j]=x->req1[j];p->req2[j]=x->req2[j];}if(sem_make(&p->go,0)||sem_make(&p->done,0)){fprintf(stderr,"patient semaphore setup failed\n");munmap(g_shm,sizeof *g_shm);shm_unlink(SHM_NAME);return 2;}g_shm->rec[i].a=i;g_shm->rec[i].b=i+1;g_shm->rec[i].sum=2*i+1;}
    int pipefd[2];if(pipe(pipefd)<0){perror("pipe");munmap(g_shm,sizeof *g_shm);shm_unlink(SHM_NAME);return 2;}g_log_fd=pipefd[1];pid_t children[2+2*MAX_WORKERS+MAX_PATIENTS];int child_count=0;pid_t logger=fork();if(logger<0){perror("fork logger");return 2;}if(logger==0){close(pipefd[1]);g_log_fd=-1;logger_process(pipefd[0],no_ui);}children[child_count++]=logger;close(pipefd[0]);
#define FORK_SERVICE(call,label) do{pid_t q=fork();if(q<0){perror("fork " label);stop_requested=1;}else if(q==0){call;_exit(0);}else children[child_count++]=q;}while(0)
    FORK_SERVICE(scheduler_process(),"scheduler");if(!no_ui)FORK_SERVICE(dashboard_process(),"dashboard");for(int i=0;i<sc.readers;i++){pid_t q=fork();if(q<0){perror("fork doctor");stop_requested=1;break;}if(q==0)doctor_process(i);children[child_count++]=q;}for(int i=0;i<sc.writers;i++){pid_t q=fork();if(q<0){perror("fork nurse");stop_requested=1;break;}if(q==0)nurse_process(i);children[child_count++]=q;}
    struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_handler=on_signal;sigemptyset(&sa.sa_mask);sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);int forked[MAX_PATIENTS]={0};
    while(!g_shm->sim_done&&!stop_requested){for(int i=0;i<sc.patient_count;i++)if(!forked[i]&&sc.patients[i].arrival<=g_shm->tick){pid_t q=fork();if(q<0){perror("fork patient");stop_requested=1;break;}if(q==0)patient_process(i);g_shm->p[i].pid=q;forked[i]=1;children[child_count++]=q;event_log("FORK",g_shm->p[i].id,"child=%ld",(long)q);}for(;;){int status;pid_t q=waitpid(-1,&status,WNOHANG);if(q<=0)break;if(q!=logger)event_log("REAPED",-1,"child=%ld status=%d",(long)q,status);for(int k=0;k<child_count;k++)if(children[k]==q)children[k]=0;}sleep_ms(20);}
    if(stop_requested){g_shm->sim_done=1;for(int i=0;i<child_count;i++)if(children[i]>0&&children[i]!=logger)kill(children[i],SIGTERM);}for(int i=0;i<child_count;i++)if(children[i]>0&&children[i]!=logger){int status;while(waitpid(children[i],&status,0)<0&&errno==EINTR){}event_log("REAPED",-1,"child=%ld status=%d",(long)children[i],status);}close(g_log_fd);g_log_fd=-1;if(logger>0){int status;while(waitpid(logger,&status,0)<0&&errno==EINTR){}}
    if(!stop_requested){double tat=0,wt=0;puts("\nFinal statistics");puts("PAT CT TAT WT");for(int i=0;i<sc.patient_count;i++){int ct=g_shm->p[i].completion_tick,t=ct-g_shm->p[i].arrival,w=t-g_shm->p[i].burst;printf("P%-2d %-2d %-3d %d\n",g_shm->p[i].id,ct,t,w);tat+=t;wt+=w;}printf("Average TAT: %.2f\nAverage WT: %.2f\n",tat/sc.patient_count,wt/sc.patient_count);printf("Records: reads=%lu writes=%lu corruption=%lu\n",g_shm->total_reads,g_shm->total_writes,g_shm->corrupt);printf("Cache FIFO H/M=%lu/%lu LRU H/M=%lu/%lu\n",g_shm->fifo_hits,g_shm->fifo_misses,g_shm->lru_hits,g_shm->lru_misses);}
    int rc=stop_requested?130:(g_shm->internal_error?2:0);for(int i=0;i<sc.patient_count;i++){sem_destroy(&g_shm->p[i].go);sem_destroy(&g_shm->p[i].done);}sem_destroy(&g_shm->banker_mutex);sem_destroy(&g_shm->vent);sem_destroy(&g_shm->rw_mutex);sem_destroy(&g_shm->rw_wrt);sem_destroy(&g_shm->cache_mutex);sem_destroy(&g_shm->feed_mutex);munmap(g_shm,sizeof *g_shm);shm_unlink(SHM_NAME);return rc;
}
