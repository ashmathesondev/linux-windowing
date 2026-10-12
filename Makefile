# Makefile: a thin driver around CMake presets. CMake stays the source of truth.
#
#   make                       configure and build with the default preset
#   make PRESET=clang-release  pick another preset (see `make presets`)
#   make help                  list everything

PRESET  ?= gcc-debug
JOBS    ?= $(shell nproc 2>/dev/null || echo 4)
BUILD   := build/$(PRESET)
CACHE   := $(BUILD)/CMakeCache.txt
PROJECT := $(notdir $(CURDIR))
PRESETS := gcc-debug gcc-release clang-debug clang-release clang-asan

.DEFAULT_GOAL := all
MAKEFLAGS += --no-print-directory
.PHONY: all configure reconfigure build rebuild test test-asan clean distclean \
        gcc clang release asan all-presets presets help deps zip docs-check FORCE

all: build ## Configure (if needed) and build with PRESET (default gcc-debug)

$(CACHE):
	cmake --preset $(PRESET)

configure: $(CACHE) ## Run CMake configure for PRESET if it has not been done yet

reconfigure: ## Force a fresh configure for PRESET
	cmake --fresh --preset $(PRESET)

build: $(CACHE) ## Build PRESET
	cmake --build --preset $(PRESET) -j$(JOBS)

rebuild: clean build ## Clean PRESET, then build it

test: build ## Build, then run the tests with PRESET
	ctest --test-dir $(BUILD) --output-on-failure -j$(JOBS)

test-asan: ## Build and test with Clang + ASan/UBSan
	$(MAKE) PRESET=clang-asan test

clean: ## Remove build outputs of PRESET but keep its configuration
	@if [ -f $(CACHE) ]; then cmake --build --preset $(PRESET) --target clean; else echo "nothing to clean for $(PRESET)"; fi

distclean: ## Delete the whole build/ directory (every preset)
	rm -rf build

gcc: ## Shortcut: PRESET=gcc-debug
	$(MAKE) PRESET=gcc-debug build

clang: ## Shortcut: PRESET=clang-debug
	$(MAKE) PRESET=clang-debug build

release: ## Shortcut: PRESET=gcc-release
	$(MAKE) PRESET=gcc-release build

asan: ## Shortcut: PRESET=clang-asan
	$(MAKE) PRESET=clang-asan build

all-presets: ## Build and test every preset, one after another
	@set -e; for p in $(PRESETS); do echo "=== $$p ==="; $(MAKE) PRESET=$$p test; done

presets: ## List the available presets
	@echo $(PRESETS)

# make run-p09, make run-p06 ... builds, then starts that stage. Environment variables such as
# WW_BACKEND=x11|wayland and WW_SCALE=2 are passed through to the program.
run-%: build FORCE ## Build, then run a stage, for example `make run-p09` (extra args: ARGS="--screenshot out")
	@dir=$$(ls -d $*_* 2>/dev/null | head -n1); \
	if [ -z "$$dir" ]; then echo "no stage directory matches $*_*"; exit 1; fi; \
	echo "running $(BUILD)/$$dir/$$dir"; \
	$(BUILD)/$$dir/$$dir $(ARGS)

deps: ## Install the Ubuntu packages needed to build (uses sudo)
	sudo apt install build-essential cmake pkg-config clang \
	    libx11-dev libxext-dev libxcb1-dev libx11-xcb-dev libxkbcommon-dev libxkbcommon-x11-dev \
	    libwayland-dev wayland-protocols libcairo2-dev libpango1.0-dev

zip: ## Create a zip of the project one level up, without build outputs
	cd .. && rm -f $(PROJECT).zip && zip -qr $(PROJECT).zip $(PROJECT) -x "$(PROJECT)/build/*" && echo "wrote ../$(PROJECT).zip"

docs-check: ## Check that every Mermaid diagram in docs/ renders (needs mmdc)
	python3 scripts/check_mermaid.py docs

FORCE:

help: ## Show this help
	@echo "Usage: make [target] [PRESET=$(PRESET)] [JOBS=$(JOBS)]"; echo
	@grep -E '^[a-zA-Z_%-]+:.*## ' $(MAKEFILE_LIST) | sed -E 's/^([^:]+):.*## /\1\t/' | awk -F'\t' '{printf "  %-16s %s\n", $$1, $$2}'
