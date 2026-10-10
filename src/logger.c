/* Assignment 4: the logger is the sole reader of the common event pipe.
 * It persists complete atomic event lines and mirrors them into a shared ring.
 * In no-UI mode it also echoes events so demonstrations remain observable.
 * EOF is the shutdown signal, reached only after every writer closes its end. */
#define _DEFAULT_SOURCE
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void publish(const char *line) {
    if (sem_wait(&g_shm->feed_mutex) < 0) return;
    snprintf(g_shm->log_lines[g_shm->log_head], LOG_LINE, "%s", line);
    g_shm->log_head = (g_shm->log_head + 1) % LOG_LINES;
    if (g_shm->log_count < LOG_LINES) g_shm->log_count++;
    sem_post(&g_shm->feed_mutex);
}

void logger_process(int read_fd, int echo) {
    FILE *log = fopen("hospital.log", "w");
    if (!log) _exit(2);
    char buf[1024], line[LOG_LINE]; size_t used=0;
    for (;;) {
        ssize_t n=read(read_fd,buf,sizeof buf);
        if(n<0){if(errno==EINTR)continue;fclose(log);_exit(2);} if(n==0)break;
        for(ssize_t i=0;i<n;i++){
            if(used<sizeof line-1)line[used++]=buf[i];
            if(buf[i]=='\n'){
                line[used]='\0';fputs(line,log);fflush(log);publish(line);
                if(echo){fputs(line,stdout);fflush(stdout);} used=0;
            }
        }
    }
    if(used){line[used]='\0';fputs(line,log);publish(line);if(echo)fputs(line,stdout);}
    fclose(log);close(read_fd);_exit(0);
}
