#ifndef MINIBOX_H
#define MINIBOX_H

#define _GNU_SOURCE


#include <stdio.h>
#include <stdlib.h>
#include <sched.h>  // 
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/mount.h> // 

#define STACK_SIZE (1024 * 1024)



typedef struct s_sandbox_config
{
    char    *hostname;
    char    *memory_limit;
    char    *cpu_limit;
    char    *pids_limit;
    char    *rootfs;
    char    **command;
}   t_sandbox_config;

typedef struct s_child_args
{
    t_sandbox_config *config;
    int              sync_fd;
}   t_child_args;

int start_sandbox(t_sandbox_config *config);

#endif