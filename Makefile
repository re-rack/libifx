# Intellifex LTD native effects and reverse-engineering workspace. Run from the
# project root.
#
#   make build                          the static library, build/default/libifx.a (the default)
#   make install                        libifx.a, the headers and ifx.pc under PREFIX (/usr/local),
#                                       DESTDIR-aware; make uninstall removes them
#   make check-install                  build a C and a C++ host against a staged install
#   make check                          the tables fixture and the host checks
#   make check-fixture FIXTURE=<name>   one fixture, or several names in quotes
#   make benchmark-all-fx               every effect on the Q23 test signal, ns per frame
#   make benchmark-fx FX=<name>         one effect
#   make check-tables                   the tables fixture
#   make check-host                     the context and the process entry points
#   make check-convert                  all 2^32 floats on every conversion path, NEON and RVV
#                                       under qemu
#   make record-fixture FIXTURE=<name>  rewrite one fixture from the firmware harness
#   make record-all-fixtures            rewrite tests/fixtures/ from the firmware harness
#   make coverage                       gcov line and branch coverage of src/
#   make sanitize                       check and the quick converter test under ASan and UBSan,
#                                       with gcc and clang
#   make format                         apply clang-format
#   make static-check                   format check, cppcheck, clang and gcc warnings and analyzers
#   make harness                        build/harness/libifxharness.a: the DSP56001 core and board,
#                                       the 80C51 core and board
#   make tools                          build/tools/ifx_dsp_disasm, the DSP disassembler, the
#                                       bring-up programs and the DSP core's memory-map check
#   make dsp-core-check                 run the vendored DSP core's memory-map check
#   make ghidra-prepare                 verify the EPROM, split it, disassemble both processors,
#                                       and build the Ghidra project (see reverse_engineering/README.md)
#   make ghidra-clean                   remove what ghidra-prepare generates, but not the Ghidra
#                                       project and its exports
#   make clean                          remove every build artifact
#
# ghidra-prepare looks for the EPROM (and the manual) by SHA-256 in the project
# root and in reverse_engineering/inputs/. The split records, disassembly, call
# graph and CSVs land under reverse_engineering/, the Ghidra GUI import files
# under build/ghidra. Everything else goes to build/.

CC ?= cc
CXX ?= c++
CFLAGS ?= -O2 -g
GCC ?= gcc
CLANG ?= clang
CLANG_FORMAT ?= clang-format
CPPCHECK ?= cppcheck
AARCH64_CC ?= aarch64-linux-gnu-gcc
QEMU_AARCH64 ?= qemu-aarch64
AARCH64_SYSROOT ?= /usr/aarch64-linux-gnu
RISCV64_CC ?= riscv64-linux-gnu-gcc
QEMU_RISCV64 ?= qemu-riscv64
RISCV64_SYSROOT ?= /usr/riscv64-linux-gnu
JOBS ?= $(shell nproc)
PYTHON ?= python3
DISASSEMBLER ?= build/tools/ifx_dsp_disasm
GHIDRA ?= /opt/ghidra
PYGHIDRA ?= $(HOME)/.local/share/gmajor2/pyghidra/bin/python
RUFF ?= ruff
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
VERSION := 0.1.0
RE := reverse_engineering
FIRMWARE := $(RE)/firmware
GHIDRA_PROJECT := $(RE)/ghidra/Intellifex.gpr
SCRIPTS := $(wildcard $(RE)/*.py)

BUILD ?= build/default
BUILD_HARNESS := build/harness
BUILD_TOOLS := build/tools

STRICT := -std=c17 -Wall -Wextra -Werror -Wpedantic -Wformat=2 -Wshadow -Wundef \
          -Wcast-qual -Wpointer-arith -Wwrite-strings -Wstrict-prototypes \
          -Wold-style-definition -Wmissing-prototypes -Wredundant-decls \
          -Wdouble-promotion -Wvla -Wswitch-enum -Wconversion -Wsign-conversion
DEPFLAGS := -MMD -MP
HARNESS := $(RE)/harness
HARNESS_DIRS := $(HARNESS) $(HARNESS)/hatari $(HARNESS)/mcs51
HARNESS_INCLUDES := $(addprefix -I,$(HARNESS_DIRS))
C_DIRS := src tests $(HARNESS_DIRS)
INCLUDES := $(addprefix -I,$(C_DIRS))

LIBRARY := $(BUILD_HARNESS)/libifxharness.a
# Programs in the harness directory own a main() and are linked, not archived.
TOOL_PROGRAMS := ifx_dsp_disasm ifx_mcu_bringup ifx_dsp_core_check
TOOL_SOURCES := $(addprefix $(HARNESS)/,$(addsuffix .c,$(TOOL_PROGRAMS)))
TOOLS := $(addprefix $(BUILD_TOOLS)/,$(TOOL_PROGRAMS))
DISASSEMBLER_TOOL := $(BUILD_TOOLS)/ifx_dsp_disasm
HARNESS_SOURCES := $(filter-out $(TOOL_SOURCES), \
                     $(wildcard $(HARNESS)/*.c) $(wildcard $(HARNESS)/hatari/*.c) \
                     $(wildcard $(HARNESS)/mcs51/*.c))
HARNESS_OBJECTS := $(patsubst $(HARNESS)/%.c,$(BUILD_HARNESS)/%.o,$(HARNESS_SOURCES))

MACHINE := $(shell $(CC) -dumpmachine)
ARCH := $(if $(findstring x86_64,$(MACHINE)),x86_64,$(if $(findstring aarch64,$(MACHINE)),aarch64,))

LIB_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard src/*.c))
LIB_HEADERS := $(wildcard src/*.h)
TEST_EFFECT_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(wildcard tests/effect_*.c))
CHECK_OBJECTS := $(BUILD)/tests/ifx_check.o $(BUILD)/tests/ifx_fixture.o $(BUILD)/tests/ifx_signal.o \
                 $(TEST_EFFECT_OBJECTS)
RECORD_OBJECTS := $(BUILD)/tests/ifx_record.o $(BUILD)/tests/ifx_fixture.o $(BUILD)/tests/ifx_signal.o \
                  $(TEST_EFFECT_OBJECTS) $(patsubst %.c,$(BUILD)/%.o,$(wildcard tests/firmware_*.c))
OBJECTS := $(sort $(LIB_OBJECTS) $(CHECK_OBJECTS) $(RECORD_OBJECTS) \
             $(BUILD)/tests/ifx_bench.o \
             $(patsubst %.c,$(BUILD)/%.o,$(wildcard tests/test_convert.c)))
DEPS := $(OBJECTS:.o=.d) $(HARNESS_OBJECTS:.o=.d) \
        $(patsubst $(HARNESS)/%.c,$(BUILD_HARNESS)/%.d,$(TOOL_SOURCES))

# Plugins are shared objects: the library has to be position-independent, and
# hiding its symbols keeps two plugins that embed different versions apart.
$(LIB_OBJECTS): LIB_FLAGS := -fPIC -fvisibility=hidden

C_FILES := $(wildcard src/*.c) $(wildcard tests/*.c) $(wildcard $(HARNESS)/*.c)
FORMAT_FILES := $(C_FILES) $(wildcard src/*.h) $(wildcard tests/*.h) $(wildcard $(HARNESS)/*.h)
ANALYSIS_FILES := $(C_FILES)

# The converter picks its path when it is compiled, so every path gets its own test binary.
CONVERT_VARIANTS := scalar
CONVERT_FLAGS_scalar := -DIFX_NO_SIMD
CONVERT_NEEDS_scalar :=
CONVERT_NAMES_scalar := scalar
ifeq ($(ARCH),x86_64)
CONVERT_VARIANTS += sse2 avx2 avx512
CONVERT_FLAGS_sse2 := -march=x86-64
CONVERT_FLAGS_avx2 := -march=x86-64-v3
CONVERT_FLAGS_avx512 := -march=x86-64-v4
CONVERT_NEEDS_sse2 := sse2
CONVERT_NEEDS_avx2 := avx2
CONVERT_NEEDS_avx512 := avx512f
CONVERT_NAMES_sse2 := SSE2
CONVERT_NAMES_avx2 := AVX2
CONVERT_NAMES_avx512 := AVX-512
else ifeq ($(ARCH),aarch64)
CONVERT_VARIANTS += neon
CONVERT_FLAGS_neon :=
CONVERT_NEEDS_neon :=
CONVERT_NAMES_neon := NEON
endif
CONVERT_BINS := $(foreach v,$(CONVERT_VARIANTS),$(BUILD)/test_convert_$(v))

# $(call convert,<path>,<arguments>)
convert = if [ -n '$(CONVERT_NEEDS_$1)' ] && ! grep -qw '$(CONVERT_NEEDS_$1)' /proc/cpuinfo; then \
            printf 'convert         SKIP  %s: not on this CPU\n' '$(CONVERT_NAMES_$1)'; \
          else $(BUILD)/test_convert_$1 --expect '$(CONVERT_NAMES_$1)' $2; fi

CONVERT_NATIVE := $(addprefix check-convert-,$(CONVERT_VARIANTS))
CONVERT_TARGETS := $(CONVERT_NATIVE)
ifeq ($(ARCH),x86_64)
CONVERT_TARGETS += check-convert-neon check-convert-rvv
endif

# The RVV path runs at 128- and 1024-bit vectors, and built for Zve32f, the smallest vector
# extension it accepts.
RVV_ARCH := rv64gcv
RVV_ARCH_ZVE32F := rv64imafdc_zve32f
RVV_CPU = rv64,v=true,vlen=$1
RVV_CPU_ZVE32F := rv64,v=false,zve32f=true,vlen=128

.DEFAULT_GOAL := build
.DELETE_ON_ERROR:

build: $(BUILD)/libifx.a

install: $(BUILD)/libifx.a
	install -d $(DESTDIR)$(LIBDIR)/pkgconfig $(DESTDIR)$(INCLUDEDIR)/ifx
	install -m 644 $(BUILD)/libifx.a $(DESTDIR)$(LIBDIR)/libifx.a
	install -m 644 $(LIB_HEADERS) $(DESTDIR)$(INCLUDEDIR)/ifx/
	printf '%s\n' 'prefix=$(PREFIX)' 'libdir=$(LIBDIR)' 'includedir=$(INCLUDEDIR)' '' \
	  'Name: ifx' 'Description: The Rocktron Intellifex LTD effects, bit-exact with the hardware' \
	  'Version: $(VERSION)' 'Cflags: -I$${includedir}/ifx' 'Libs: -L$${libdir} -lifx' \
	  > $(DESTDIR)$(LIBDIR)/pkgconfig/ifx.pc

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/libifx.a $(DESTDIR)$(LIBDIR)/pkgconfig/ifx.pc
	rm -rf $(DESTDIR)$(INCLUDEDIR)/ifx

INSTALL_CHECK := build/install-check
CXX_STRICT := -std=c++17 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Wsign-conversion

check-install:
	@rm -rf $(INSTALL_CHECK)
	@$(MAKE) --no-print-directory install PREFIX=$(CURDIR)/$(INSTALL_CHECK) >/dev/null
	@for header in $(notdir $(wildcard src/ifx_*_setup.h)); do \
	  grep -q "#include \"$$header\"" src/ifx.h || \
	    { echo "check-install: ifx.h does not include $$header" >&2; exit 1; }; \
	done
	@export PKG_CONFIG_PATH=$(CURDIR)/$(INSTALL_CHECK)/lib/pkgconfig && \
	  $(CC) $(STRICT) $(CFLAGS) $$($(PKG_CONFIG) --cflags ifx) -o $(INSTALL_CHECK)/host_c \
	    tests/test_install.c $$($(PKG_CONFIG) --libs ifx) && \
	  $(CXX) $(CXX_STRICT) $(CFLAGS) $$($(PKG_CONFIG) --cflags ifx) -o $(INSTALL_CHECK)/host_cxx \
	    -x c++ tests/test_install.c -x none $$($(PKG_CONFIG) --libs ifx) && \
	  $(INSTALL_CHECK)/host_c && $(INSTALL_CHECK)/host_cxx

check: $(BUILD)/ifx_check
	@$(BUILD)/ifx_check

benchmark-all-fx: $(BUILD)/ifx_bench
	@$(BUILD)/ifx_bench

benchmark-fx: $(BUILD)/ifx_bench
	@if [ -z "$(FX)" ]; then echo 'usage: make benchmark-fx FX=<name>' >&2; exit 1; fi
	@$(BUILD)/ifx_bench $(FX)

check-fixture: $(BUILD)/ifx_check
	@if [ -z "$(FIXTURE)" ]; then echo 'usage: make check-fixture FIXTURE=<name>' >&2; exit 1; fi
	@$(BUILD)/ifx_check $(FIXTURE)

check-tables: $(BUILD)/ifx_check
	@$(BUILD)/ifx_check tables

check-host: $(BUILD)/ifx_check
	@$(BUILD)/ifx_check host

check-convert: $(CONVERT_TARGETS)

$(CONVERT_NATIVE): check-convert-%: $(BUILD)/test_convert_%
	@$(call convert,$*,--exhaustive)

ifeq ($(ARCH),x86_64)
check-convert-neon:
	@mkdir -p $(BUILD)
	@if command -v $(AARCH64_CC) >/dev/null 2>&1 && command -v $(QEMU_AARCH64) >/dev/null 2>&1; then \
	  $(AARCH64_CC) $(STRICT) $(CFLAGS) -static $(INCLUDES) -o $(BUILD)/test_convert_neon_aarch64 \
	    tests/test_convert.c src/ifx_convert.c -lm && \
	  $(QEMU_AARCH64) $(BUILD)/test_convert_neon_aarch64 --expect NEON --exhaustive; \
	else \
	  printf 'convert         SKIP  NEON: no %s or %s\n' '$(AARCH64_CC)' '$(QEMU_AARCH64)'; \
	fi

check-convert-rvv:
	@mkdir -p $(BUILD)
	@if command -v $(RISCV64_CC) >/dev/null 2>&1 && command -v $(QEMU_RISCV64) >/dev/null 2>&1; then \
	  $(RISCV64_CC) $(STRICT) $(CFLAGS) -march=$(RVV_ARCH) -static $(INCLUDES) \
	    -o $(BUILD)/test_convert_rvv_riscv64 tests/test_convert.c src/ifx_convert.c -lm && \
	  $(RISCV64_CC) $(STRICT) $(CFLAGS) -march=$(RVV_ARCH_ZVE32F) -static $(INCLUDES) \
	    -o $(BUILD)/test_convert_zve32f_riscv64 tests/test_convert.c src/ifx_convert.c -lm && \
	  $(QEMU_RISCV64) -cpu $(call RVV_CPU,128) $(BUILD)/test_convert_rvv_riscv64 \
	    --expect RVV --exhaustive && \
	  $(QEMU_RISCV64) -cpu $(call RVV_CPU,1024) $(BUILD)/test_convert_rvv_riscv64 --expect RVV && \
	  $(QEMU_RISCV64) -cpu $(RVV_CPU_ZVE32F) $(BUILD)/test_convert_zve32f_riscv64 --expect RVV; \
	else \
	  printf 'convert         SKIP  RVV: no %s or %s\n' '$(RISCV64_CC)' '$(QEMU_RISCV64)'; \
	fi
endif

record-all-fixtures: $(BUILD)/ifx_record
	@mkdir -p tests/fixtures
	@names=$$($(BUILD)/ifx_record --list) && \
	  printf '%s\n' $$names | xargs -P $(JOBS) -I{} $(BUILD)/ifx_record {} tests/fixtures/{}.bin

record-fixture: $(BUILD)/ifx_record
	@if [ -z "$(FIXTURE)" ]; then echo 'usage: make record-fixture FIXTURE=<name>' >&2; exit 1; fi
	@mkdir -p tests/fixtures
	@for name in $(FIXTURE); do \
	  $(BUILD)/ifx_record $$name tests/fixtures/$$name.bin || exit 1; \
	done

harness: $(LIBRARY)

tools: $(TOOLS)

ghidra-prepare: $(DISASSEMBLER_TOOL)
	@$(PYTHON) $(RE)/build_workspace.py --output $(RE) --disassembler $(DISASSEMBLER)
	@$(PYTHON) $(RE)/build_ghidra.py --firmware $(FIRMWARE) --prepare build/ghidra
	@if [ -e $(GHIDRA_PROJECT) ]; then \
	  echo '$(GHIDRA_PROJECT) exists and is kept'; \
	elif [ -d $(GHIDRA) ] && $(PYGHIDRA) -c 'import pyghidra' 2>/dev/null; then \
	  $(PYGHIDRA) $(RE)/build_ghidra.py --ghidra $(GHIDRA) --firmware $(FIRMWARE); \
	else \
	  echo 'SKIP the Ghidra project: it needs Ghidra in $(GHIDRA) and PyGhidra in $(PYGHIDRA)'; \
	  echo '     (GHIDRA= and PYGHIDRA= move them)'; \
	fi

# The exports stay with the project: only building the project writes them.
ghidra-clean:
	rm -rf build/ghidra $(FIRMWARE) \
	  $(addprefix $(RE)/,asm callgraph memory_map.csv entrypoints.csv seeds.csv)

format:
	@$(CLANG_FORMAT) -i $(FORMAT_FILES)

dsp-core-check: $(BUILD_TOOLS)/ifx_dsp_core_check
	@$(BUILD_TOOLS)/ifx_dsp_core_check

static-check: static-format static-cppcheck static-clang static-gcc static-ruff

static-ruff:
	@$(RUFF) check $(SCRIPTS)
	@$(RUFF) format --check $(SCRIPTS)

static-format:
	@$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

# cppcheck 2.22 has no c17; C17 adds no rule cppcheck models, so c11 is the
# closest standard it accepts.
static-cppcheck:
	@$(CPPCHECK) --enable=warning,style,performance,portability \
	  --inline-suppr --error-exitcode=1 --suppress=missingIncludeSystem --quiet \
	  --std=c11 $(INCLUDES) $(C_FILES)

# $(call clang_check,<flags>,<file>): clang --analyze silences ordinary warnings, so they get their own
# pass.
clang_check = $(CLANG) $(STRICT) $(INCLUDES) $1 -fsyntax-only $2 && \
              $(CLANG) --analyze -Xclang -analyzer-werror -Xclang -analyzer-output=text \
                -o /dev/null -std=c17 $(INCLUDES) $1 $2

# $(call gcc_check,<compiler>,<flags>,<file>): the flow warnings need -O2; the analyzer sees most at
# -O0, before the optimizer deletes code.
gcc_check = $1 $(STRICT) -O2 $(INCLUDES) $2 -c $3 -o /dev/null && \
            $1 $(STRICT) -O0 -fanalyzer $(INCLUDES) $2 -c $3 -o /dev/null

static-clang:
	@printf '%s\n' $(C_FILES) | xargs -P $(JOBS) -n 1 sh -c '$(call clang_check,,"$$0")'
ifeq ($(ARCH),x86_64)
	@for flags in -DIFX_NO_SIMD -march=x86-64-v3 -march=x86-64-v4; do \
	  $(call clang_check,$$flags,src/ifx_convert.c) || exit 1; \
	done
	@if [ -d $(AARCH64_SYSROOT) ]; then \
	  $(call clang_check,--target=aarch64-linux-gnu --sysroot=$(AARCH64_SYSROOT),src/ifx_convert.c); \
	else \
	  echo 'SKIP static-clang NEON path: no $(AARCH64_SYSROOT)'; \
	fi
	@if [ -d $(RISCV64_SYSROOT) ]; then \
	  for arch in $(RVV_ARCH) $(RVV_ARCH_ZVE32F); do \
	    $(call clang_check,--target=riscv64-linux-gnu -march=$$arch --sysroot=$(RISCV64_SYSROOT),src/ifx_convert.c) || exit 1; \
	  done; \
	else \
	  echo 'SKIP static-clang RVV path: no $(RISCV64_SYSROOT)'; \
	fi
else
	@echo 'SKIP static-clang: the other conversion paths are checked on x86-64 only'
endif
	@echo 'static-clang: $(words $(C_FILES)) files clean'

static-gcc:
	@printf '%s\n' $(C_FILES) | xargs -P $(JOBS) -n 1 sh -c '$(call gcc_check,$(GCC),,"$$0")'
ifeq ($(ARCH),x86_64)
	@for flags in -DIFX_NO_SIMD -march=x86-64-v3 -march=x86-64-v4; do \
	  $(call gcc_check,$(GCC),$$flags,src/ifx_convert.c) || exit 1; \
	done
	@if command -v $(AARCH64_CC) >/dev/null 2>&1; then \
	  $(call gcc_check,$(AARCH64_CC),,src/ifx_convert.c); \
	else \
	  echo 'SKIP static-gcc NEON path: no $(AARCH64_CC)'; \
	fi
	@if command -v $(RISCV64_CC) >/dev/null 2>&1; then \
	  for arch in $(RVV_ARCH) $(RVV_ARCH_ZVE32F); do \
	    $(call gcc_check,$(RISCV64_CC),-march=$$arch,src/ifx_convert.c) || exit 1; \
	  done; \
	else \
	  echo 'SKIP static-gcc RVV path: no $(RISCV64_CC)'; \
	fi
else
	@echo 'SKIP static-gcc: the other conversion paths are checked on x86-64 only'
endif
	@echo 'static-gcc: $(words $(C_FILES)) files clean'

SANITIZE_FLAGS := -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
                  -fno-omit-frame-pointer

sanitize: sanitize-gcc sanitize-clang

sanitize-gcc:
	@$(MAKE) --no-print-directory BUILD=build/sanitize-gcc CC='$(GCC)' \
	  CFLAGS='$(SANITIZE_FLAGS)' exercise

sanitize-clang:
	@$(MAKE) --no-print-directory BUILD=build/sanitize-clang CC='$(CLANG)' \
	  CFLAGS='$(SANITIZE_FLAGS)' exercise

exercise: $(BUILD)/ifx_check $(CONVERT_BINS)
	@$(BUILD)/ifx_check
	@$(foreach v,$(CONVERT_VARIANTS),$(call convert,$(v),) &&) true

# gcov runs inside build/coverage, so the sources are recorded by absolute path.
# Each converter test compiles its own ifx_convert.c, and all of them count.
coverage:
	@rm -rf build/coverage
	@$(MAKE) --no-print-directory BUILD=build/coverage \
	  CFLAGS='-O0 -g --coverage -fprofile-abs-path' exercise
	@cd build/coverage && gcov -b src/*.gcda test_convert_*-ifx_convert.gcda 2>/dev/null | \
	  awk -v prefix='$(CURDIR)/src/' '/^File / { path = substr($$2, 2, length($$2) - 2); next } \
	    /^Lines executed:/ { lines = substr($$0, 16); wanted = (index(path, prefix) == 1); next } \
	    /^Branches executed:/ { branches = substr($$0, 19); next } \
	    /^No branches/ { branches = "none"; next } \
	    /^Calls executed:|^No calls/ { if (wanted) { name = path; sub(/.*\//, "", name); \
	    if (!(name in seen)) { seen[name] = 1; \
	    printf "%-16s lines %-18s branches %s\n", name, lines, branches } } \
	    wanted = 0; next }'

clean:
	rm -rf build

$(LIBRARY): $(HARNESS_OBJECTS)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $^

$(BUILD)/libifx.a: $(LIB_OBJECTS)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $^

$(BUILD)/ifx_check: $(CHECK_OBJECTS) $(BUILD)/libifx.a
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) -o $@ $^ -lm

$(BUILD)/ifx_bench: $(BUILD)/tests/ifx_bench.o $(BUILD)/tests/ifx_signal.o $(TEST_EFFECT_OBJECTS) $(BUILD)/libifx.a
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) -o $@ $^ -lm

$(BUILD)/ifx_record: $(RECORD_OBJECTS) $(BUILD)/libifx.a $(LIBRARY)
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) -o $@ $(RECORD_OBJECTS) $(BUILD)/libifx.a $(LIBRARY) -lm

$(BUILD)/test_convert_%: tests/test_convert.c src/ifx_convert.c src/ifx_convert.h Makefile
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) $(CONVERT_FLAGS_$*) $(INCLUDES) -o $@ \
	  tests/test_convert.c src/ifx_convert.c -lm

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) $(LIB_FLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_TOOLS)/%: $(BUILD_HARNESS)/%.o $(LIBRARY)
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

$(BUILD_HARNESS)/%.o: $(HARNESS)/%.c Makefile
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) $(DEPFLAGS) $(HARNESS_INCLUDES) -c -o $@ $<

-include $(DEPS)

.PHONY: build install uninstall check-install check check-fixture check-tables check-host \
        benchmark-all-fx benchmark-fx check-convert $(CONVERT_TARGETS) record-all-fixtures record-fixture harness tools \
        ghidra-prepare ghidra-clean format dsp-core-check static-check static-ruff static-format \
        static-cppcheck static-clang static-gcc sanitize sanitize-gcc sanitize-clang exercise \
        coverage clean
