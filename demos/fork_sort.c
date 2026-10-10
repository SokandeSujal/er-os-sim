/* Assignment 2a: fork creates independent parent and child address spaces.
 * Each process sorts its private copy of the same input array.
 * The child prints descending order and the parent prints ascending order.
 * wait() ensures the parent explicitly reaps the child before exiting. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
static int asc(const void*a,const void*b){int x=*(const int*)a,y=*(const int*)b;return(x>y)-(x<y);}
static int desc(const void*a,const void*b){return asc(b,a);}
static void show(const char*name,int*a,int n){printf("%s:",name);for(int i=0;i<n;i++)printf(" %d",a[i]);putchar('\n');}
int main(void){int n;printf("How many integers? ");fflush(stdout);if(scanf("%d",&n)!=1||n<1||n>1000){fprintf(stderr,"Enter a count from 1 to 1000.\n");return 1;}int*a=malloc((size_t)n*sizeof*a);if(!a){perror("malloc");return 2;}printf("Enter %d integers: ",n);fflush(stdout);for(int i=0;i<n;i++)if(scanf("%d",&a[i])!=1){fprintf(stderr,"Invalid integer.\n");free(a);return 1;}fflush(NULL);pid_t p=fork();if(p<0){perror("fork");free(a);return 2;}if(p==0){qsort(a,(size_t)n,sizeof*a,desc);show("Child descending",a,n);fflush(stdout);free(a);_exit(0);}qsort(a,(size_t)n,sizeof*a,asc);show("Parent ascending",a,n);int status;if(waitpid(p,&status,0)<0){perror("waitpid");free(a);return 2;}free(a);return WIFEXITED(status)?WEXITSTATUS(status):2;}
