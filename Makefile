CC ?= cc
OPTFLAGS ?= -O2
WARNFLAGS = -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
# Use `make WERROR=` to keep warnings visible without failing the build
# (e.g. when trying a new cluster compiler for the first time).
WERROR ?= -Werror
# Plain assignment so an environment CFLAGS cannot silently drop the warnings.
CFLAGS = -std=c11 $(OPTFLAGS) $(WARNFLAGS) $(WERROR)
CPPFLAGS = -Iinclude
LDLIBS = -lm
# OpenMP flags. GCC on the cluster takes -fopenmp. On macOS (Apple clang +
# Homebrew libomp) override them, e.g.:
#   make OMP_CFLAGS='-Xpreprocessor -fopenmp -I/opt/homebrew/opt/libomp/include' \
#        OMP_LDLIBS='-L/opt/homebrew/opt/libomp/lib -lomp'
OMP_CFLAGS ?= -fopenmp
OMP_LDLIBS ?= -fopenmp
# MPI compiler wrapper for the hybrid backend (needs the OpenMPI module).
MPICC ?= mpicc

GIT_COMMIT := $(shell git describe --always --dirty --abbrev=12 2>/dev/null || echo unavailable)
BUILD_INFO = $(GIT_COMMIT) | $(CC) | $(CFLAGS)
BUILD_INFO_CPPFLAGS = -DNBODY_COMPILER='"$(CC)"' \
                      -DNBODY_BUILD_FLAGS='"$(CFLAGS)"' \
                      -DNBODY_GIT_COMMIT='"$(GIT_COMMIT)"'

BUILD_DIR := build
HEADERS := include/nbody.h include/config.h include/report.h
COMMON_SOURCES := src/particles.c src/integrator.c src/diagnostics.c \
                  src/config.c src/report.c
SERIAL_SOURCES := $(COMMON_SOURCES) src/force_serial.c

.PHONY: all test test-hybrid hybrid clean FORCE

all: $(BUILD_DIR)/nbody_serial $(BUILD_DIR)/nbody_openmp

$(BUILD_DIR):
	mkdir -p $@

# Rewritten only when the commit, compiler or flags change, so binaries are
# rebuilt whenever their embedded build metadata would be stale.
$(BUILD_DIR)/build_info: FORCE | $(BUILD_DIR)
	@echo '$(BUILD_INFO)' | cmp -s - $@ || echo '$(BUILD_INFO)' > $@

$(BUILD_DIR)/nbody_serial: $(SERIAL_SOURCES) src/main_serial.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(BUILD_INFO_CPPFLAGS) $(CFLAGS) $(SERIAL_SOURCES) src/main_serial.c -o $@ $(LDLIBS)

$(BUILD_DIR)/nbody_openmp: $(COMMON_SOURCES) src/force_openmp.c src/main_openmp.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(BUILD_INFO_CPPFLAGS) $(CFLAGS) $(OMP_CFLAGS) $(COMMON_SOURCES) src/force_openmp.c src/main_openmp.c -o $@ $(LDLIBS) $(OMP_LDLIBS)

# Hybrid MPI+OpenMP backend. Not part of `all` until OpenMPI is in
# env/modules.sh; build it explicitly with `make hybrid`.
HYBRID_SOURCES := $(COMMON_SOURCES) src/force_openmp.c src/decomposition.c

hybrid: $(BUILD_DIR)/nbody_hybrid

$(BUILD_DIR)/nbody_hybrid: $(HYBRID_SOURCES) src/main_hybrid.c $(HEADERS) $(BUILD_DIR)/build_info
	$(MPICC) $(CPPFLAGS) -DNBODY_COMPILER='"$(MPICC)"' -DNBODY_BUILD_FLAGS='"$(CFLAGS)"' -DNBODY_GIT_COMMIT='"$(GIT_COMMIT)"' $(CFLAGS) $(OMP_CFLAGS) $(HYBRID_SOURCES) src/main_hybrid.c -o $@ $(LDLIBS) $(OMP_LDLIBS)

# partition_range unit tests: plain C, no MPI, runs anywhere.
$(BUILD_DIR)/test_hybrid: src/decomposition.c tests/test_hybrid.c include/nbody.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/decomposition.c tests/test_hybrid.c -o $@ $(LDLIBS)

test-hybrid: $(BUILD_DIR)/test_hybrid
	./$(BUILD_DIR)/test_hybrid

$(BUILD_DIR)/test_nbody: $(SERIAL_SOURCES) tests/test_nbody.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SERIAL_SOURCES) tests/test_nbody.c -o $@ $(LDLIBS)

test: $(BUILD_DIR)/nbody_serial $(BUILD_DIR)/test_nbody
	./$(BUILD_DIR)/test_nbody
	sh tests/test_cli.sh

clean:
	rm -rf $(BUILD_DIR)
