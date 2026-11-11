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

# OpenACC backend. Built on the cluster with NVHPC, not part of `all`:
#     NBODY_MODULES=nvhpc/25.7 . env/modules.sh && make openacc
# NVHPC's math header warns (issue #1), so this target has no -Werror, and
# nvc does not take the GCC warning set. CPU-only check without a GPU
# (OpenACC host fallback):
#     make openacc ACC_CC=gcc ACC_FLAGS="-fopenacc -Wall -Wextra"
ACC_CC ?= nvc
ACC_FLAGS ?= -acc -gpu=cc89,cc120 -Minfo=accel
ACC_CFLAGS = -std=c11 $(OPTFLAGS) $(ACC_FLAGS)
ACC_BUILD_INFO = $(GIT_COMMIT) | $(ACC_CC) | $(ACC_CFLAGS)
ACC_SOURCES := $(COMMON_SOURCES) src/force_openacc.c

.PHONY: all test clean openacc test_openacc FORCE

all: $(BUILD_DIR)/nbody_serial

$(BUILD_DIR):
	mkdir -p $@

# Rewritten only when the commit, compiler or flags change, so binaries are
# rebuilt whenever their embedded build metadata would be stale.
$(BUILD_DIR)/build_info: FORCE | $(BUILD_DIR)
	@echo '$(BUILD_INFO)' | cmp -s - $@ || echo '$(BUILD_INFO)' > $@

$(BUILD_DIR)/nbody_serial: $(SERIAL_SOURCES) src/main_serial.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(BUILD_INFO_CPPFLAGS) $(CFLAGS) $(SERIAL_SOURCES) src/main_serial.c -o $@ $(LDLIBS)

$(BUILD_DIR)/test_nbody: $(SERIAL_SOURCES) tests/test_nbody.c $(HEADERS) $(BUILD_DIR)/build_info
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SERIAL_SOURCES) tests/test_nbody.c -o $@ $(LDLIBS)

openacc: $(BUILD_DIR)/nbody_openacc

$(BUILD_DIR)/build_info_openacc: FORCE | $(BUILD_DIR)
	@echo '$(ACC_BUILD_INFO)' | cmp -s - $@ || echo '$(ACC_BUILD_INFO)' > $@

$(BUILD_DIR)/nbody_openacc: $(ACC_SOURCES) src/main_openacc.c $(HEADERS) $(BUILD_DIR)/build_info_openacc
	$(ACC_CC) $(CPPFLAGS) -DNBODY_COMPILER='"$(ACC_CC)"' \
	    -DNBODY_BUILD_FLAGS='"$(ACC_CFLAGS)"' -DNBODY_GIT_COMMIT='"$(GIT_COMMIT)"' \
	    $(ACC_CFLAGS) $(ACC_SOURCES) src/main_openacc.c -o $@ $(LDLIBS)

$(BUILD_DIR)/test_openacc: $(ACC_SOURCES) src/force_serial.c tests/test_openacc.c $(HEADERS)
	$(ACC_CC) $(CPPFLAGS) $(ACC_CFLAGS) $(ACC_SOURCES) src/force_serial.c tests/test_openacc.c -o $@ $(LDLIBS)

test_openacc: $(BUILD_DIR)/test_openacc
	./$(BUILD_DIR)/test_openacc

test: $(BUILD_DIR)/nbody_serial $(BUILD_DIR)/test_nbody
	./$(BUILD_DIR)/test_nbody
	sh tests/test_cli.sh

clean:
	rm -rf $(BUILD_DIR)
