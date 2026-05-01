#include <stdio.h>
#include "minibox.h"
#include "parser.h"

/*
 * main() stays intentionally small.
 *
 * Its job is only to:
 *   1. parse CLI arguments
 *   2. store them in the config struct
 *   3. start the sandbox
 *
 * All namespace, cgroup, and rootfs logic lives in separate modules.
 */
int main(int argc, char **argv)
{
    t_sandbox_config config;

    if (parse_arguments(argc, argv, &config) != 0)
        return 1;

    if (start_sandbox(&config) != 0)
    {
        fprintf(stderr, "Error: failed to start sandbox\n");
        return 1;
    }

    return 0;
}