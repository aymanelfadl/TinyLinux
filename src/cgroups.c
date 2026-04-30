#include "cgroups.h"

static int write_file(const char *path, const char *value)
{
    int     fd;
    size_t  len;
    ssize_t written;

    fd = open(path, O_WRONLY);
    if (fd == -1)
    {
        perror(path);
        return 1;
    }

    len = strlen(value);
    written = write(fd, value, len);
    if (written == -1 || (size_t)written != len)
    {
        perror(path);
        close(fd);
        return 1;
    }

    if (close(fd) == -1)
    {
        perror(path);
        return 1;
    }

    return 0;
}

static int write_pid_file(const char *path, pid_t pid)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%d", pid);
    return write_file(path, buffer);
}

static unsigned long parse_memory_to_bytes(const char *value)
{
    char            *end;
    unsigned long   number;

    number = strtoul(value, &end, 10);

    if (*end == 'K' && end[1] == '\0')
        return number * 1024;
    if (*end == 'M' && end[1] == '\0')
        return number * 1024 * 1024;
    if (*end == 'G' && end[1] == '\0')
        return number * 1024 * 1024 * 1024;
    if (*end == '\0')
        return number;

    return 0;
}

static int set_memory_limit(const char *value)
{
    char            buffer[64];
    unsigned long   bytes;

    bytes = parse_memory_to_bytes(value);
    if (bytes == 0)
    {
        fprintf(stderr, "Error: invalid memory limit: %s\n", value);
        return 1;
    }

    snprintf(buffer, sizeof(buffer), "%lu", bytes);

    if (write_file(CGROUP_PATH "/memory.max", buffer))
        return 1;

    if (write_file(CGROUP_PATH "/memory.swap.max", "0"))
        return 1;

    return 0;
}

static int set_pids_limit(const char *value)
{
    return write_file(CGROUP_PATH "/pids.max", value);
}

static int set_cpu_limit(const char *value)
{
    char    buffer[64];
    long    cpu_percent;
    long    quota;
    char    *end;

    cpu_percent = strtol(value, &end, 10);
    if (*end != '\0' || cpu_percent < 1 || cpu_percent > 100)
    {
        fprintf(stderr, "Error: CPU limit must be between 1 and 100\n");
        return 1;
    }

    quota = cpu_percent * 1000;
    snprintf(buffer, sizeof(buffer), "%ld 100000", quota);

    return write_file(CGROUP_PATH "/cpu.max", buffer);
}

int setup_cgroup(t_sandbox_config *config, pid_t child_pid)
{
    if (mkdir(CGROUP_PATH, 0755) == -1 && errno != EEXIST)
    {
        perror("mkdir cgroup");
        return 1;
    }

    if (config->memory_limit && set_memory_limit(config->memory_limit))
        return 1;

    if (config->pids_limit && set_pids_limit(config->pids_limit))
        return 1;

    if (config->cpu_limit && set_cpu_limit(config->cpu_limit))
        return 1;

    if (write_pid_file(CGROUP_PATH "/cgroup.procs", child_pid))
        return 1;

    return 0;
}

int cleanup_cgroup(void)
{
    if (rmdir(CGROUP_PATH) == -1)
    {
        perror("rmdir cgroup");
        return 1;
    }

    return 0;
}