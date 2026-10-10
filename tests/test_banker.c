/* Assignment 5 test: exercises the pure Banker's safety and request algorithms.
 * Cases cover the manual golden sequence and every request return status.
 * No IPC is involved, so failures point directly to algorithm logic. */
#include "common.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x,n) do{if(x)printf("PASS %s\n",n);else{printf("FAIL %s\n",n);fail++;}}while(0)
int main(void){int fail=0,avail[3]={0,1,2},a[3][3]={{1,2,1},{2,0,1},{2,2,1}},need[3][3]={{1,0,3},{0,1,2},{1,2,0}},seq[3];CHECK(banker_is_safe(3,3,avail,(const int(*)[3])a,(const int(*)[3])need,seq)&&seq[0]==1&&seq[1]==0&&seq[2]==2,"golden safe sequence");int ua[2][3]={{1,0,0},{0,1,0}},un[2][3]={{0,1,0},{1,0,0}},uv[3]={0,0,0};CHECK(!banker_is_safe(2,3,uv,(const int(*)[3])ua,(const int(*)[3])un,seq),"unsafe detected");int av[3]={1,1,1},al[2][3]={{0}},ne[2][3]={{1,1,1},{1,1,1}},r[3]={2,0,0};CHECK(banker_try_request(2,3,0,r,av,al,ne,seq)==ERROR_EXCEEDS_MAX,"above need");r[0]=1;av[0]=0;CHECK(banker_try_request(2,3,0,r,av,al,ne,seq)==WAIT_UNAVAILABLE,"above available");int av2[3]={1,0,0},al2[2][3]={{1,0,0},{0,0,0}},ne2[2][3]={{1,0,0},{2,0,0}};CHECK(banker_try_request(2,3,1,r,av2,al2,ne2,seq)==WAIT_UNSAFE,"available but unsafe");return fail?1:0;}
