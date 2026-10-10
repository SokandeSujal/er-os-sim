/* Assignment 7 test: checks FIFO, LRU, Optimal and Belady's anomaly.
 * Both online policies receive identical reference strings and frame counts.
 * Expected totals are fixed examples from the operating-systems manual. */
#include "common.h"
#include <stdio.h>
#define CHECK(x,n) do{if(x)printf("PASS %s\n",n);else{printf("FAIL %s\n",n);fail++;}}while(0)
static void init(CacheState*s,int n){s->size=n;s->next=0;s->clock=0;for(int i=0;i<MAX_PATIENTS;i++){s->frames[i]=-1;s->stamp[i]=0;}}
static int fifo_faults(const int*r,int n,int frames){CacheState s;init(&s,frames);int f=0;for(int i=0;i<n;i++)f+=!fifo_access(&s,r[i]);return f;}
int main(void){int fail=0,r[]={1,2,3,4,2,1,5,6,2,1,2,3,7,6,3,2,1,2,3,6},n=20;CacheState f,l;init(&f,3);init(&l,3);int ff=0,lf=0;for(int i=0;i<n;i++){ff+=!fifo_access(&f,r[i]);lf+=!lru_access(&l,r[i]);}CHECK(ff==16&&lf==15&&optimal_faults(r,n,3)==11,"manual reference totals");int better[]={1,2,3,1,4,1,2};init(&f,3);init(&l,3);ff=lf=0;for(int i=0;i<7;i++){ff+=!fifo_access(&f,better[i]);lf+=!lru_access(&l,better[i]);}CHECK(lf<ff,"LRU beats FIFO");int b[]={1,2,3,4,1,2,5,1,2,3,4,5};CHECK(fifo_faults(b,12,3)==9&&fifo_faults(b,12,4)==10,"Belady anomaly");return fail?1:0;}
