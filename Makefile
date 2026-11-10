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

.PHONY: all test clean FORCE

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

$(BUILD_DIR)/test_nbody: $(SERIAL_SOURCES) tests/test_nbody.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SERIAL_SOURCES) tests/test_nbody.c -o $@ $(LDLIBS)

test: $(BUILD_DIR)/nbody_serial $(BUILD_DIR)/test_nbody
	./$(BUILD_DIR)/test_nbody
	sh tests/test_cli.sh

clean:
	rm -rf $(BUILD_DIR)
