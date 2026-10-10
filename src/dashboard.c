/* Assignments 3-7: ANSI dashboard observes shared state without controlling it.
 * Panels summarize scheduling, resources, records, caches and recent pipe events.
 * It uses the alternate screen and restores terminal state on every normal exit.
 * Small terminals receive a clear resize request instead of a broken layout. */
#define _DEFAULT_SOURCE
#include "common.h"
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void frames(const CacheState*s){putchar('[');for(int i=0;i<s->size;i++)printf(i?" %d":"%d",s->frames[i]);putchar(']');}
static const char *colour(int state){if(state==DONE)return "\033[32m";if(state==RUNNING)return "\033[36m";if(state==WAIT_RES||state==WAIT_VENT)return "\033[33m";if(state==READY)return "\033[34m";return "\033[0m";}
static volatile sig_atomic_t dashboard_stop;
static void stop_dashboard(int sig){(void)sig;dashboard_stop=1;}
void dashboard_process(void){
    signal(SIGINT,stop_dashboard);signal(SIGTERM,stop_dashboard);
    printf("\033[?1049h\033[?25l");fflush(stdout);
    while(!g_shm->sim_done&&!dashboard_stop){struct winsize ws={0};ioctl(STDOUT_FILENO,TIOCGWINSZ,&ws);printf("\033[H\033[2J");if(ws.ws_col&& (ws.ws_col<100||ws.ws_row<32)){puts("Hospital ER Simulator: enlarge terminal to at least 100 x 32");fflush(stdout);sleep_ms(200);continue;}
        printf("HOSPITAL ER  tick=%d  policy=%s  running=%d  tick=%dms\n",g_shm->tick,g_shm->policy==POLICY_SRTF?"SRTF":"FCFS",g_shm->running_id,g_shm->tick_ms);
        puts("ID  STATE      SEV  REM/BURST  VENT  ALLOCATION");for(int i=0;i<g_shm->patient_count;i++){Patient*p=&g_shm->p[i];printf("%-3d %s%-10s\033[0m %-4d %3d/%-5d %-5s [%d %d %d]\n",p->id,colour(p->state),state_name(p->state),p->severity,p->remaining,p->burst,p->has_vent?"yes":"no",p->alloc[0],p->alloc[1],p->alloc[2]);}
        printf("Gantt: ");int start=g_shm->gantt_len>40?g_shm->gantt_len-40:0;for(int i=start;i<g_shm->gantt_len;i++)putchar(g_shm->gantt[i]);putchar('\n');int vent_value=0;sem_getvalue(&g_shm->vent,&vent_value);printf("Ventilators: [");for(int i=0;i<g_shm->ventilators;i++)putchar(i<g_shm->ventilators-vent_value?'#':' ');printf("] %d/%d used\n",g_shm->ventilators-vent_value,g_shm->ventilators);
        printf("Banker total=[%d %d %d] available=[%d %d %d] safe=",g_shm->total[0],g_shm->total[1],g_shm->total[2],g_shm->available[0],g_shm->available[1],g_shm->available[2]);for(int i=0;i<g_shm->safe_len;i++)printf("P%d%s",g_shm->safe_seq[i],i+1==g_shm->safe_len?"":"->");putchar('\n');puts("Banker allocation / need:");for(int i=0;i<g_shm->patient_count;i++){Patient*p=&g_shm->p[i];if(p->state!=EMPTY&&p->state!=DONE)printf(" P%-2d A=[%d %d %d] N=[%d %d %d]\n",p->id,p->alloc[0],p->alloc[1],p->alloc[2],p->max[0]-p->alloc[0],p->max[1]-p->alloc[1],p->max[2]-p->alloc[2]);}
        unsigned long ft=g_shm->fifo_hits+g_shm->fifo_misses,lt=g_shm->lru_hits+g_shm->lru_misses;double fr=ft?100.0*g_shm->fifo_hits/ft:0.0,lr=lt?100.0*g_shm->lru_hits/lt:0.0;printf("Records: readers=%d writer=%s reads=%lu writes=%lu corruption=%lu\n",g_shm->read_count,g_shm->writer_active?"yes":"no",g_shm->total_reads,g_shm->total_writes,g_shm->corrupt);printf("Cache FIFO ");frames(&g_shm->fifo);printf(" H/M=%lu/%lu %.1f%%   LRU ",g_shm->fifo_hits,g_shm->fifo_misses,fr);frames(&g_shm->lru);printf(" H/M=%lu/%lu %.1f%%\n",g_shm->lru_hits,g_shm->lru_misses,lr);puts("Recent events:");sem_wait(&g_shm->feed_mutex);for(int i=0;i<g_shm->log_count;i++){int at=(g_shm->log_head-g_shm->log_count+i+LOG_LINES)%LOG_LINES;fputs(g_shm->log_lines[at],stdout);}sem_post(&g_shm->feed_mutex);fflush(stdout);sleep_ms(200);
    }
    printf("\033[?25h\033[?1049l");fflush(stdout);_exit(0);
}
