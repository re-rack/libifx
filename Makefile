# Intellifex LTD effects: reverse-engineering workspace and checks. Run from the project root.
#
#   make ghidra-prepare   verify the EPROM, split it, disassemble both processors, and build the
#                         Ghidra project (see reverse_engineering/README.md)
#   make ghidra-clean     remove what ghidra-prepare generates, but not the Ghidra project and
#                         its exports
#   make static-check     ruff check and ruff format --check on the workspace scripts
#   make clean            remove build/
#
# ghidra-prepare looks for the EPROM (and the manual) by SHA-256 in the project root and in
# reverse_engineering/inputs/. The split records, disassembly, call graph and CSVs land under
# reverse_engineering/, the Ghidra GUI import files under build/ghidra.

PYTHON ?= python3
DISASSEMBLER ?= dsp56300-disasm
GHIDRA ?= /opt/ghidra
PYGHIDRA ?= $(HOME)/.local/share/gmajor2/pyghidra/bin/python
RUFF ?= ruff
RE := reverse_engineering
FIRMWARE := $(RE)/firmware
GHIDRA_PROJECT := $(RE)/ghidra/Intellifex.gpr
SCRIPTS := $(wildcard $(RE)/*.py)

.DEFAULT_GOAL := ghidra-prepare

ghidra-prepare:
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

static-check:
	@$(RUFF) check $(SCRIPTS)
	@$(RUFF) format --check $(SCRIPTS)

clean:
	rm -rf build

.PHONY: ghidra-prepare ghidra-clean static-check clean
