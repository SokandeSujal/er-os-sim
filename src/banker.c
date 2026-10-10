/* Assignment 5: Banker's safety and resource-request algorithms.
 * A tentative allocation is committed only when every active process can finish.
 * The pure functions operate on arrays for deterministic unit testing.
 * The wrapper serialises shared matrices with a process-shared semaphore. */
#include "common.h"
#include <errno.h>
#include <string.h>

bool banker_is_safe(int n, int m, const int avail[], const int alloc[][RESOURCES],
                    const int need[][RESOURCES], int safe_seq[]) {
    int work[RESOURCES] = {0}; bool finish[MAX_PATIENTS] = {false}; int count = 0;
    for (int j=0;j<m;j++) work[j]=avail[j];
    while (count < n) {
        int found = -1;
        for (int i=0;i<n && found<0;i++) if (!finish[i]) {
            int j; for (j=0;j<m;j++) if (need[i][j] > work[j]) break;
            if (j == m) found = i;
        }
        if (found < 0) return false;
        for (int j=0;j<m;j++) work[j] += alloc[found][j];
        finish[found] = true; if (safe_seq) safe_seq[count] = found; count++;
    }
    return true;
}

BankerResult banker_try_request(int n, int m, int who, const int request[],
                                int avail[], int alloc[][RESOURCES],
                                int need[][RESOURCES], int safe_seq[]) {
    for (int j=0;j<m;j++) if (request[j] > need[who][j]) return ERROR_EXCEEDS_MAX;
    for (int j=0;j<m;j++) if (request[j] > avail[j]) return WAIT_UNAVAILABLE;
    for (int j=0;j<m;j++) { avail[j]-=request[j]; alloc[who][j]+=request[j]; need[who][j]-=request[j]; }
    if (banker_is_safe(n,m,avail,(const int (*)[RESOURCES])alloc,(const int (*)[RESOURCES])need,safe_seq)) return GRANTED;
    for (int j=0;j<m;j++) { avail[j]+=request[j]; alloc[who][j]-=request[j]; need[who][j]+=request[j]; }
    return WAIT_UNSAFE;
}

BankerResult banker_request(int slot, const int request[]) {
    if (sem_wait(&g_shm->banker_mutex) < 0) { event_log("ERROR", g_shm->p[slot].id, "banker sem_wait: %s", strerror(errno)); return ERROR_EXCEEDS_MAX; }
    int map[MAX_PATIENTS], n=0, who=-1, alloc[MAX_PATIENTS][RESOURCES]={{0}}, need[MAX_PATIENTS][RESOURCES]={{0}}, seq[MAX_PATIENTS];
    for (int i=0;i<g_shm->patient_count;i++) if (g_shm->p[i].state != EMPTY && g_shm->p[i].state != DONE) {
        map[n]=i; if(i==slot) who=n;
        for(int j=0;j<RESOURCES;j++){alloc[n][j]=g_shm->p[i].alloc[j];need[n][j]=g_shm->p[i].max[j]-alloc[n][j];} n++;
    }
    BankerResult result = who < 0 ? ERROR_EXCEEDS_MAX : banker_try_request(n,RESOURCES,who,request,g_shm->available,alloc,need,seq);
    if (result == GRANTED) {
        for(int i=0;i<n;i++) for(int j=0;j<RESOURCES;j++) g_shm->p[map[i]].alloc[j]=alloc[i][j];
        g_shm->safe_len=n; for(int i=0;i<n;i++) g_shm->safe_seq[i]=g_shm->p[map[seq[i]]].id;
    }
    if (sem_post(&g_shm->banker_mutex) < 0) event_log("ERROR", g_shm->p[slot].id, "banker sem_post: %s", strerror(errno));
    return result;
}

int banker_release(int slot) {
    if (sem_wait(&g_shm->banker_mutex) < 0) return -1;
    for(int j=0;j<RESOURCES;j++){g_shm->available[j]+=g_shm->p[slot].alloc[j];g_shm->p[slot].alloc[j]=0;}
    return sem_post(&g_shm->banker_mutex);
}
