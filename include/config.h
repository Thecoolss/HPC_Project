#ifndef NBODY_CONFIG_H
#define NBODY_CONFIG_H

#include "nbody.h"

/* Command-line options that are not part of the physical configuration. */
typedef struct {
    int check;               /* --check: report energy/momentum diagnostics */
    const char *output_path; /* --output FILE: final state; NULL if absent */
} RunOptions;

typedef enum {
    CONFIG_INVALID = 0,
    CONFIG_OK = 1,
    CONFIG_HELP = 2
} ConfigStatus;

/* Fills defaults, then applies argv. Shared by every backend executable. */
ConfigStatus config_parse(int argc, char **argv, SimulationConfig *config,
                          RunOptions *options);
void config_print_usage(const char *program);

#endif
