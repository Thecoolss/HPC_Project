#include "config.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--n N] [--steps S] [--dt DT] [--eps EPS] "
            "[--seed SEED] [--check] [--output FILE]\n",
            program);
}

static int parse_positive_int(const char *text, int *value)
{
    char *end = NULL;
    errno = 0;
    const long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < 1 || parsed > INT_MAX) {
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

static int parse_positive_double(const char *text, double *value)
{
    char *end = NULL;
    errno = 0;
    const double parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' ||
        !isfinite(parsed) || parsed <= 0.0) {
        return 0;
    }
    *value = parsed;
    return 1;
}

static int parse_seed(const char *text, uint64_t *value)
{
    char *end = NULL;
    errno = 0;
    if (text[0] == '-') {
        return 0;
    }
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > UINT64_MAX) {
        return 0;
    }
    *value = (uint64_t)parsed;
    return 1;
}

ConfigStatus config_parse(int argc, char **argv, SimulationConfig *config,
                          RunOptions *options)
{
    *config = (SimulationConfig){
        .n = 1000,
        .steps = 100,
        .dt = 0.001,
        .epsilon = 0.01,
        .G = 1.0,
        .seed = 42
    };
    *options = (RunOptions){0};

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--check") == 0) {
            options->check = 1;
            continue;
        }
        if (strcmp(argv[i], "--help") == 0) {
            config_print_usage(argv[0]);
            return CONFIG_HELP;
        }
        if (i + 1 >= argc) {
            return CONFIG_INVALID;
        }

        const char *option = argv[i];
        const char *value = argv[++i];
        int valid = 0;
        if (strcmp(option, "--n") == 0) {
            valid = parse_positive_int(value, &config->n);
        } else if (strcmp(option, "--steps") == 0) {
            valid = parse_positive_int(value, &config->steps);
        } else if (strcmp(option, "--dt") == 0) {
            valid = parse_positive_double(value, &config->dt);
        } else if (strcmp(option, "--eps") == 0) {
            valid = parse_positive_double(value, &config->epsilon);
        } else if (strcmp(option, "--seed") == 0) {
            valid = parse_seed(value, &config->seed);
        } else if (strcmp(option, "--output") == 0) {
            valid = value[0] != '\0';
            options->output_path = value;
        }
        if (!valid) {
            return CONFIG_INVALID;
        }
    }

    return CONFIG_OK;
}
