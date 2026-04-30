#ifndef CGROUPS_H
#define CGROUPS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

#include "minibox.h"

#define CGROUP_PATH "/sys/fs/cgroup/minibox"

int setup_cgroup(t_sandbox_config *config, pid_t child_pid);
int cleanup_cgroup(void);

#endif