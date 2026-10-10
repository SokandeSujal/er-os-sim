/* Assignment 2c: the child exits while its parent deliberately does not wait.
 * Its exit status remains in the process table, producing a zombie/defunct entry.
 * The parent prints inspection instructions and sleeps for thirty seconds.
 * It finally calls waitpid so the demonstration cleans up after itself. */
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
int main(void){pid_t p=fork();if(p<0){perror("fork");return 2;}if(p==0)_exit(0);printf("Parent PID=%ld, zombie child PID=%ld\nRun: ps -el | grep defunct\n",(long)getpid(),(long)p);fflush(stdout);sleep(30);if(waitpid(p,NULL,0)<0){perror("waitpid");return 2;}puts("Child reaped.");return 0;}
