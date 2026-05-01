#ifndef USERNS_H
#define USERNS_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>

int setup_user_namespace(pid_t child_pid);

#endif