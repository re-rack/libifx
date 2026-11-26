# Intellifex LTD effects: reverse-engineering workspace and checks. Run from the project root.
#
#   make harness          build/harness/libifxharness.a: the DSP56001 core and board, the
#                         80C51 core and board
#   make tools            build/tools/ifx_dsp_disasm, the DSP disassembler that replaces
#                         dsp56300-disasm, the bring-up programs and the DSP core's memory-map
#                         check
#   make ghidra-prepare   verify the EPROM, split it, disassemble both processors, and build the
#                         Ghidra project (see reverse_engineering/README.md)
#   make ghidra-clean     remove what ghidra-prepare generates, but not the Ghidra project and
#                         its exports
#   make format           apply clang-format to the harness sources, but not to the vendored
#                         cpu cores under harness/hatari and harness/mcs51
#   make dsp-core-check   run the vendored DSP core's memory-map check
#   make static-check     clang-format check, cppcheck, gcc and clang warnings and analyzers on
#                         the harness sources, then ruff on the workspace scripts
#   make clean            remove build/
#
# ghidra-prepare looks for the EPROM (and the manual) by SHA-256 in the project root and in
# reverse_engineering/inputs/. The split records, disassembly, call graph and CSVs land under
# reverse_engineering/, the Ghidra GUI import files under build/ghidra.

CC ?= cc
CFLAGS ?= -O2 -g
GCC ?= gcc
CLANG ?= clang
CLANG_FORMAT ?= clang-format
CPPCHECK ?= cppcheck
JOBS ?= $(shell nproc)
PYTHON ?= python3
DISASSEMBLER ?= build/tools/ifx_dsp_disasm
GHIDRA ?= /opt/ghidra
PYGHIDRA ?= $(HOME)/.local/share/gmajor2/pyghidra/bin/python
RUFF ?= ruff
RE := reverse_engineering
FIRMWARE := $(RE)/firmware
GHIDRA_PROJECT := $(RE)/ghidra/Intellifex.gpr
SCRIPTS := $(wildcard $(RE)/*.py)

HARNESS := $(RE)/harness
BUILD_HARNESS := build/harness
BUILD_TOOLS := build/tools

STRICT := -std=c17 -Wall -Wextra -Werror -Wpedantic -Wformat=2 -Wshadow -Wundef \
          -Wcast-qual -Wpointer-arith -Wwrite-strings -Wstrict-prototypes \
          -Wold-style-definition -Wmissing-prototypes -Wredundant-decls \
          -Wdouble-promotion -Wvla -Wswitch-enum -Wconversion -Wsign-conversion
DEPFLAGS := -MMD -MP
HARNESS_DIRS := $(HARNESS) $(HARNESS)/hatari $(HARNESS)/mcs51
HARNESS_INCLUDES := $(addprefix -I,$(HARNESS_DIRS))

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
DEPS := $(HARNESS_OBJECTS:.o=.d) $(patsubst $(HARNESS)/%.c,$(BUILD_HARNESS)/%.d,$(TOOL_SOURCES))

# The vendored cores keep their upstream formatting and their own history.
FORMAT_FILES := $(wildcard $(HARNESS)/*.c) $(wildcard $(HARNESS)/*.h)
ANALYSIS_FILES := $(wildcard $(HARNESS)/*.c)

.DEFAULT_GOAL := ghidra-prepare
.DELETE_ON_ERROR:

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
	  --std=c11 $(HARNESS_INCLUDES) $(ANALYSIS_FILES)

# $(call clang_check,<file>): clang --analyze silences ordinary warnings, so they get their own
# pass.
clang_check = $(CLANG) $(STRICT) $(HARNESS_INCLUDES) -fsyntax-only $1 && \
              $(CLANG) --analyze -Xclang -analyzer-werror -Xclang -analyzer-output=text \
                -o /dev/null -std=c17 $(HARNESS_INCLUDES) $1

# $(call gcc_check,<compiler>,<file>): the flow warnings need -O2; the analyzer sees most at
# -O0, before the optimizer deletes code.
gcc_check = $1 $(STRICT) -O2 $(HARNESS_INCLUDES) $2 -c -o /dev/null && \
            $1 $(STRICT) -O0 -fanalyzer $(HARNESS_INCLUDES) $2 -c -o /dev/null

static-clang:
	@printf '%s\n' $(ANALYSIS_FILES) | xargs -P $(JOBS) -n 1 sh -c '$(call clang_check,"$$0")'
	@echo 'static-clang: $(words $(ANALYSIS_FILES)) files clean'

static-gcc:
	@printf '%s\n' $(ANALYSIS_FILES) | xargs -P $(JOBS) -n 1 sh -c '$(call gcc_check,$(GCC),"$$0")'
	@echo 'static-gcc: $(words $(ANALYSIS_FILES)) files clean'

clean:
	rm -rf build

$(LIBRARY): $(HARNESS_OBJECTS)
	@mkdir -p $(@D)
	$(RM) $@
	$(AR) rcs $@ $^

$(BUILD_TOOLS)/%: $(BUILD_HARNESS)/%.o $(LIBRARY)
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm

$(BUILD_HARNESS)/%.o: $(HARNESS)/%.c Makefile
	@mkdir -p $(@D)
	$(CC) $(STRICT) $(CFLAGS) $(DEPFLAGS) $(HARNESS_INCLUDES) -c -o $@ $<

-include $(DEPS)

.PHONY: harness tools ghidra-prepare ghidra-clean format dsp-core-check static-check \
        static-ruff static-format static-cppcheck static-clang static-gcc clean
