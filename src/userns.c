#include "userns.h"

/*
 * User namespace configuration is done by writing to files in /proc/<pid>/.
 *
 * Important files:
 *   uid_map   -> maps UIDs inside the namespace to UIDs outside
 *   gid_map   -> maps GIDs inside the namespace to GIDs outside
 *   setgroups -> must usually be disabled before writing gid_map
 */
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

    close(fd);
    return 0;
}

/*
 * When the program is run with sudo, getuid() returns 0.
 * But we want to map root inside the namespace to the original user,
 * not to real host root.
 *
 * sudo stores the original user ID in SUDO_UID.
 */
static uid_t get_host_uid(void)
{
    char *sudo_uid;

    sudo_uid = getenv("SUDO_UID");
    if (sudo_uid)
        return (uid_t)atoi(sudo_uid);
    return getuid();
}

/*
 * Same idea as get_host_uid(), but for the group ID.
 */
static gid_t get_host_gid(void)
{
    char *sudo_gid;

    sudo_gid = getenv("SUDO_GID");
    if (sudo_gid)
        return (gid_t)atoi(sudo_gid);
    return getgid();
}

/*
 * Configure UID/GID mapping for the child user namespace.
 *
 * Mapping format:
 *   inside_id outside_id count
 *
 * Example:
 *   0 1000 1
 *
 * Meaning:
 *   UID 0 inside the namespace maps to UID 1000 on the host.
 *
 * So the process can be root inside the sandbox without being real root
 * on the host filesystem.
 */
int setup_user_namespace(pid_t child_pid)
{
    char path[256];
    char map[256];

    /*
     * Linux requires setgroups to be disabled before writing gid_map
     * in many unprivileged user namespace setups.
     */
    snprintf(path, sizeof(path), "/proc/%d/setgroups", child_pid);
    write_file(path, "deny");

    snprintf(path, sizeof(path), "/proc/%d/uid_map", child_pid);
    snprintf(map, sizeof(map), "0 %d 1\n", get_host_uid());
    if (write_file(path, map))
        return 1;

    snprintf(path, sizeof(path), "/proc/%d/gid_map", child_pid);
    snprintf(map, sizeof(map), "0 %d 1\n", get_host_gid());
    if (write_file(path, map))
        return 1;

    return 0;
}