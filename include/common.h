#ifndef ER_SIM_COMMON_H
#define ER_SIM_COMMON_H

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <semaphore.h>
#include <stddef.h>
#include <sys/types.h>

#define SHM_NAME "/er_sim_shm"
#define MAX_PATIENTS 16
#define RESOURCES 3
#define MAX_GANTT 256
#define LOG_LINES 10
#define LOG_LINE 256
#define MAX_WORKERS 8

typedef enum { POLICY_FCFS, POLICY_SRTF } Policy;
typedef enum { EMPTY, ARRIVED, WAIT_VENT, WAIT_RES, READY, RUNNING, DONE } PatientState;
typedef enum { GRANTED, WAIT_UNAVAILABLE, WAIT_UNSAFE, ERROR_EXCEEDS_MAX } BankerResult;

typedef struct {
    int id, arrival, burst, severity, needs_vent;
    int max[RESOURCES], req1[RESOURCES], req2[RESOURCES], req2_at;
} PatientSpec;

typedef struct {
    int policy, tick_ms, ventilators, total[RESOURCES];
    int readers, writers, cache_size, patient_count;
    PatientSpec patients[MAX_PATIENTS];
} Scenario;

typedef struct {
    int id;
    pid_t pid;
    volatile int state;
    int arrival, burst;
    volatile int remaining;
    int severity, needs_vent;
    volatile int has_vent;
    int max[RESOURCES], alloc[RESOURCES], req1[RESOURCES], req2[RESOURCES], req2_at;
    volatile int start_tick, completion_tick;
    sem_t go, done;
} Patient;

typedef struct { volatile int a, b, sum, version; } Record;

typedef struct {
    int size;
    int frames[MAX_PATIENTS];
    unsigned long stamp[MAX_PATIENTS];
    unsigned long clock;
    int next;
} CacheState;

typedef struct {
    int id, arrival, burst, severity;
} SchedProc;

typedef struct {
    int completion[MAX_PATIENTS];
    int turnaround[MAX_PATIENTS];
    int waiting[MAX_PATIENTS];
    double avg_turnaround, avg_waiting;
} SchedResult;

typedef struct {
    int policy, tick_ms, no_sync, no_ui, seed;
    int ventilators, total[RESOURCES], available[RESOURCES];
    int readers, writers, cache_size, patient_count;
    volatile int tick, sim_done, internal_error, running_id;
    Patient p[MAX_PATIENTS];
    sem_t banker_mutex, vent;
    int safe_seq[MAX_PATIENTS], safe_len;
    Record rec[MAX_PATIENTS];
    sem_t rw_mutex, rw_wrt;
    volatile int read_count, writer_active;
    volatile unsigned long total_reads, total_writes, corrupt;
    sem_t cache_mutex, feed_mutex;
    CacheState fifo, lru;
    volatile unsigned long fifo_hits, fifo_misses, lru_hits, lru_misses;
    char gantt[MAX_GANTT];
    int gantt_len;
    char log_lines[LOG_LINES][LOG_LINE];
    int log_head, log_count;
} Shared;

extern Shared *g_shm;
extern int g_log_fd;

int load_scenario(const char *path, Scenario *out, char *err, size_t err_len);
const char *state_name(int state);
void event_log(const char *type, int patient_id, const char *fmt, ...);
int sleep_ms(int ms);

bool banker_is_safe(int n, int m, const int avail[],
                    const int alloc[][RESOURCES], const int need[][RESOURCES],
                    int safe_seq[]);
BankerResult banker_try_request(int n, int m, int who, const int request[],
                                int avail[], int alloc[][RESOURCES],
                                int need[][RESOURCES], int safe_seq[]);
BankerResult banker_request(int slot, const int request[]);
int banker_release(int slot);

int srtf_pick(const SchedProc p[], const int remaining[], int n, int tick);
int fcfs_pick(const SchedProc p[], const int remaining[], int n, int tick,
              int current);
int sched_simulate(const SchedProc p[], int n, Policy policy, SchedResult *out);
void scheduler_process(void);

int fifo_access(CacheState *state, int page);
int lru_access(CacheState *state, int page);
int optimal_faults(const int refs[], int count, int frames);
void cache_access_shared(int patient_id);
int cache_demo(void);

void patient_process(int slot);
void logger_process(int read_fd, int echo_stdout);
void dashboard_process(void);
void doctor_process(int index);
void nurse_process(int index);

#endif
