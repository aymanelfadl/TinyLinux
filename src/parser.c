#include "parser.h"

static void init_config(t_sandbox_config *config)
{
    config->hostname = "minibox";
    config->memory_limit = NULL;
    config->cpu_limit = NULL;
    config->pids_limit = NULL;
    config->command = NULL;
}

static void print_usage(const char *program_name)
{
    fprintf(stderr,
        "Usage:\n"
        "  sudo %s [options] -- <command> [args...]\n\n"
        "Options:\n"
        "  --hostname <name>     Set isolated hostname\n"
        "  --mem <limit>         Set memory limit, example: 100M\n"
        "  --cpu <limit>         Set CPU limit percentage, example: 50\n"
        "  --pids <limit>        Set max number of processes, example: 20\n\n"
        "Example:\n"
        "  sudo %s --hostname box1 --mem 100M --cpu 50 --pids 20 -- /bin/bash\n",
        program_name,
        program_name
    );
}

static int is_option(const char *arg, const char *option)
{
    return strcmp(arg, option) == 0;
}

static int require_value(int index, int argc, const char *option_name)
{
    if (index + 1 >= argc)
    {
        fprintf(stderr, "Error: %s requires a value\n", option_name);
        return 1;
    }
    return 0;
}

static int is_valid_hostname(const char *value)
{
    int i;

    if (!value || value[0] == '\0')
    {
        fprintf(stderr, "Error: hostname cannot be empty\n");
        return 0;
    }
    i = 0;
    while (value[i])
    {
        if (!isalnum((unsigned char)value[i]) && value[i] != '-' && value[i] != '_')
        {
            fprintf(stderr, "Error: invalid hostname: %s\n", value);
            return 0;
        }
        i++;
    }
    return 1;
}

static int is_valid_memory_limit(const char *value)
{
    int i;

    if (!value || !isdigit((unsigned char)value[0]))
    {
        fprintf(stderr, "Error: memory limit must start with a number\n");
        return 0;
    }
    i = 0;
    while (isdigit((unsigned char)value[i]))
        i++;
    if (value[i] == '\0')
        return 1;
    if ((value[i] == 'K' || value[i] == 'M' || value[i] == 'G') && value[i + 1] == '\0')
        return 1;
    fprintf(stderr, "Error: invalid memory limit: %s. Use examples like 512K, 100M, 1G\n", value);
    return 0;
}

static int is_valid_cpu_limit(const char *value)
{
    char *end;
    long cpu;

    if (!value || value[0] == '\0')
    {
        fprintf(stderr, "Error: CPU limit cannot be empty\n");
        return 0;
    }
    cpu = strtol(value, &end, 10);
    if (*end != '\0' || cpu < 1 || cpu > 100)
    {
        fprintf(stderr, "Error: CPU limit must be between 1 and 100\n");
        return 0;
    }
    return 1;
}

static int is_valid_pids_limit(const char *value)
{
    char *end;
    long pids;

    if (!value || value[0] == '\0')
    {
        fprintf(stderr, "Error: pids limit cannot be empty\n");
        return 0;
    }
    pids = strtol(value, &end, 10);
    if (*end != '\0' || pids < 1)
    {
        fprintf(stderr, "Error: pids limit must be a positive integer\n");
        return 0;
    }
    return 1;
}


int parse_arguments(int argc, char **argv, t_sandbox_config *config)
{
    int i;

    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    init_config(config);

    i = 1;
    while (i < argc)
    {
        if (is_option(argv[i], "--"))
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: missing command after --\n");
                print_usage(argv[0]);
                return 1;
            }
            config->command = &argv[i + 1];
            return 0;
        }
        else if (is_option(argv[i], "--hostname"))
        {
            if (require_value(i, argc, "--hostname"))
                return 1;
            if (!is_valid_hostname(argv[i + 1]))
                return 1;
            config->hostname = argv[i + 1];
            i += 2;
        }
        else if (is_option(argv[i], "--mem"))
        {
            if (require_value(i, argc, "--mem"))
                return 1;
            if (!is_valid_memory_limit(argv[i + 1]))
                return 1;
            config->memory_limit = argv[i + 1];
            i += 2;
        }
        else if (is_option(argv[i], "--cpu"))
        {
            if (require_value(i, argc, "--cpu"))
                return 1;
            if (!is_valid_cpu_limit(argv[i + 1]))
                return 1;
            config->cpu_limit = argv[i + 1];
            i += 2;
        }
        else if (is_option(argv[i], "--pids"))
        {
            if (require_value(i, argc, "--pids"))
                return 1;
            if (!is_valid_pids_limit(argv[i + 1]))
                return 1;
            config->pids_limit = argv[i + 1];
            i += 2;
}
        else
        {
            fprintf(stderr, "Error: unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    fprintf(stderr, "Error: missing -- before command\n");
    print_usage(argv[0]);
    return 1;
}