/* Assignment 3 test: verifies pure SRTF and FCFS simulations.
 * The four-process data and expected completion metrics come from the lab manual.
 * Exact averages catch both selection and tick-boundary errors. */
#include "common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x,n) do{if(x)printf("PASS %s\n",n);else{printf("FAIL %s\n",n);fail++;}}while(0)
int main(void){SchedProc p[4]={{1,0,5,1},{2,1,3,1},{3,2,8,1},{4,3,6,1}};SchedResult s,f;int fail=0;sched_simulate(p,4,POLICY_SRTF,&s);int sct[4]={8,4,22,14},stat[4]={8,3,20,11},swt[4]={3,0,12,5};CHECK(!memcmp(s.completion,sct,sizeof sct)&&!memcmp(s.turnaround,stat,sizeof stat)&&!memcmp(s.waiting,swt,sizeof swt)&&fabs(s.avg_turnaround-10.5)<.001&&fabs(s.avg_waiting-5.0)<.001,"SRTF manual example");sched_simulate(p,4,POLICY_FCFS,&f);int fct[4]={5,8,16,22},ftat[4]={5,7,14,19},fwt[4]={0,4,6,13};CHECK(!memcmp(f.completion,fct,sizeof fct)&&!memcmp(f.turnaround,ftat,sizeof ftat)&&!memcmp(f.waiting,fwt,sizeof fwt)&&fabs(f.avg_turnaround-11.25)<.001&&fabs(f.avg_waiting-5.75)<.001,"FCFS manual example");return fail?1:0;}
