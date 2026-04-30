#include "minibox.h"
#include "cgroups.h"


static int child_main(void *arg)
{
    t_child_args      *child_args;
    t_sandbox_config  *config;

    child_args = (t_child_args *)arg;
    config = child_args->config;

    // Change hostname only inside the new UTS namespace.
    if (sethostname(config->hostname, strlen(config->hostname)) == -1)
    {
        perror("sethostname");
        return 1;
    }

    /*
        There are three important modes:
            - hared   → mount changes can propagate to other namespaces
            - private  → mount changes stay only in this namespace
            - slave    → this namespace receives changes from another, but does not send changes back
    */
    // Make mount changes private so they do not propagate to the host.
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
    {
        perror("mount private");
        return 1;
    }

    // Mount a new /proc matching this PID namespace.
    if (mount("proc", "/proc", "proc", 0, NULL) == -1)
    {
        perror("mount proc");
        return 1;
    }


    // Replace child process with the target command.
    execvp(config->command[0], config->command);

    perror("execvp");
    return 1;
}

int start_sandbox(t_sandbox_config *config)
{
    char          *stack;
    char          *stack_top;
    pid_t         child_pid;
    int           status;
    t_child_args  child_args;

    // clone() needs a manually allocated stack for the child.
    stack = malloc(STACK_SIZE);
    if (!stack)
    {
        perror("malloc");
        return 1;
    }

    // Stack grows downward, so pass the end of the memory block.
    stack_top = stack + STACK_SIZE;
    child_args.config = config;

    // Create child in a new namespaces.
    child_pid = clone(
        child_main,
        stack_top,
        CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS |
        SIGCHLD, // When the child exits, notify the parent with SIGCHLD.
        &child_args
    );

    if (child_pid == -1)
    {
        perror("clone");
        free(stack);
        return 1;
    }
    fprintf(stdout, "[parent] child host PID: %d\n", child_pid);

    if (setup_cgroup(config, child_pid) != 0)
    {
        free(stack);
        return 1;
    }

    // Wait until the sandboxed command exits.
    if (waitpid(child_pid, &status, 0) == -1)
    {
        perror("waitpid");
        free(stack);
        return 1;
    }

    free(stack);
    cleanup_cgroup();

   if (WIFEXITED(status)) { return WEXITSTATUS(status); }
    if (WIFSIGNALED(status))
    {
        fprintf(stderr, "[parent] child killed by signal %d\n", WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }

    return 1;
}