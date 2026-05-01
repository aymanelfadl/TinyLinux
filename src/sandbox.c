#include "minibox.h"
#include "cgroups.h"
#include "userns.h"

/*
 * The child waits on this pipe until the parent finishes setup:
 * - user namespace UID/GID mapping
 * - cgroup limits
 *
 * This avoids a race where the child starts running before it is configured.
 */
static int wait_for_parent(int sync_fd)
{
    char c;

    if (read(sync_fd, &c, 1) != 1)
    {
        perror("read sync");
        return 1;
    }
    close(sync_fd);
    return 0;
}

/*
 * chroot() changes what the process sees as "/".
 *
 * Before:
 *   / -> host filesystem
 *
 * After:
 *   / -> config->rootfs
 *
 * This is basic filesystem isolation.
 */
static int setup_rootfs(const char *rootfs)
{
    if (!rootfs)
        return 0;

    if (chroot(rootfs) == -1)
    {
        perror("chroot");
        return 1;
    }

    if (chdir("/") == -1)
    {
        perror("chdir");
        return 1;
    }

    return 0;
}

/*
 * This function runs inside the cloned child process.
 *
 * The child starts in new namespaces, waits for the parent setup,
 * then configures its own isolated environment before execvp().
 */
static int child_main(void *arg)
{
    t_child_args      *child_args;
    t_sandbox_config  *config;

    child_args = (t_child_args *)arg;
    config = child_args->config;

    /*
     * Wait until the parent writes UID/GID maps and cgroup limits.
     */
    if (wait_for_parent(child_args->sync_fd) != 0)
        return 1;

    /*
     * Become UID 0/GID 0 inside the user namespace.
     *
     * This is not necessarily real host root. It depends on uid_map/gid_map.
     */
    if (setgid(0) == -1)
    {
        perror("setgid");
        return 1;
    }

    if (setuid(0) == -1)
    {
        perror("setuid");
        return 1;
    }

    /*
     * UTS namespace:
     * changing hostname here affects only this namespace.
     */
    if (sethostname(config->hostname, strlen(config->hostname)) == -1)
    {
        perror("sethostname");
        return 1;
    }

    /*
     * Mount namespace:
     * make mount events private so they do not propagate to the host.
     */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
    {
        perror("mount private");
        return 1;
    }

    /*
     * Filesystem isolation:
     * after this, "/" becomes config->rootfs.
     */
    if (setup_rootfs(config->rootfs) != 0)
        return 1;

    /*
     * Mount /proc after chroot().
     *
     * This makes /proc exist inside the new rootfs and match
     * the sandbox PID namespace.
     *
     * rootfs/proc must exist.
     */
    if (mount("proc", "/proc", "proc", 0, NULL) == -1)
    {
        perror("mount proc");
        return 1;
    }

    /*
     * Replace this setup process with the target command.
     * The namespaces and cgroup membership stay attached.
     */
    execvp(config->command[0], config->command);

    perror("execvp");
    return 1;
}

/*
 * Convert child exit status into a useful return code.
 */
static int handle_child_status(int status)
{
    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    if (WIFSIGNALED(status))
    {
        fprintf(stderr, "[parent] child killed by signal %d\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }

    return 1;
}

/*
 * start_sandbox() runs in the parent process.
 *
 * Parent responsibilities:
 * - create sync pipe
 * - allocate child stack
 * - clone child with new namespaces
 * - configure user namespace mappings
 * - configure cgroup limits
 * - wake the child
 * - wait for the child
 * - cleanup cgroup
 */
int start_sandbox(t_sandbox_config *config)
{
    char          *stack;
    char          *stack_top;
    pid_t         child_pid;
    int           status;
    t_child_args  child_args;
    int           sync_pipe[2];

    if (pipe(sync_pipe) == -1)
    {
        perror("pipe");
        return 1;
    }

    /*
     * clone() needs a manually allocated stack for the child.
     */
    stack = malloc(STACK_SIZE);
    if (!stack)
    {
        perror("malloc");
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        return 1;
    }

    /*
     * On most architectures, the stack grows downward.
     * So we pass the end of the allocated memory block.
     */
    stack_top = stack + STACK_SIZE;

    child_args.config = config;
    child_args.sync_fd = sync_pipe[0];

    /*
     * Create child with new namespaces:
     *
     * CLONE_NEWUTS   -> isolated hostname
     * CLONE_NEWPID   -> isolated PID view
     * CLONE_NEWNS    -> isolated mount table
     * CLONE_NEWUSER  -> isolated UID/GID mapping
     * CLONE_NEWNET   -> isolated network stack
     * SIGCHLD        -> parent can waitpid() the child
     */
    child_pid = clone(
        child_main,
        stack_top,
        CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS |
        CLONE_NEWUSER | CLONE_NEWNET | SIGCHLD,
        &child_args
    );

    if (child_pid == -1)
    {
        perror("clone");
        close(sync_pipe[0]);
        close(sync_pipe[1]);
        free(stack);
        return 1;
    }

    /*
     * Parent does not read from the pipe.
     */
    close(sync_pipe[0]);

    /*
     * Configure UID/GID mapping for the child user namespace.
     * Must happen before the child calls setuid(0)/setgid(0).
     */
    if (setup_user_namespace(child_pid) != 0)
    {
        close(sync_pipe[1]);
        free(stack);
        return 1;
    }

    /*
     * Configure cgroup limits and move the child into the cgroup.
     * child_pid is the host-side PID, which cgroup.procs expects.
     */
    if (setup_cgroup(config, child_pid) != 0)
    {
        close(sync_pipe[1]);
        free(stack);
        return 1;
    }

    /*
     * Wake the child after all parent-side setup is complete.
     */
    if (write(sync_pipe[1], "x", 1) != 1)
    {
        perror("write sync");
        close(sync_pipe[1]);
        cleanup_cgroup();
        free(stack);
        return 1;
    }

    close(sync_pipe[1]);

    /*
     * Wait until the sandboxed command exits.
     */
    if (waitpid(child_pid, &status, 0) == -1)
    {
        perror("waitpid");
        cleanup_cgroup();
        free(stack);
        return 1;
    }

    cleanup_cgroup();
    free(stack);

    return handle_child_status(status);
}