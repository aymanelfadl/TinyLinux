#ifndef PARSER_H
#define PARSER_H

#include "minibox.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

int parse_arguments(int argc, char **argv, t_sandbox_config *config);

#endif