/* Assignment 2b: the parent exits immediately after creating a child.
 * The child records its parent PID, sleeps, and checks that PID again.
 * Linux reparents the orphan to PID 1 or a configured subreaper such as systemd.
 * The delay makes the reparenting transition easy to observe. */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void){pid_t p=fork();if(p<0){perror("fork");return 2;}if(p>0){printf("Parent PID=%ld exiting; child PID=%ld\n",(long)getpid(),(long)p);fflush(stdout);usleep(100000);return 0;}printf("Child PID=%ld PPID before=%ld\n",(long)getpid(),(long)getppid());fflush(stdout);sleep(3);printf("Child PID=%ld PPID after=%ld (init/systemd/subreaper)\n",(long)getpid(),(long)getppid());fflush(stdout);return 0;}
