PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=ggsql
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# ---------------------------------------------------------------------------
# Local formatter shims
#
# duckdb/scripts/format.py hard-requires clang-format 11.x and cmake-format.
# Point it at `clang-format@11` from Homebrew (CI pins the same version), and
# stub cmake-format since we have no CMake files under src/test to format.
# ---------------------------------------------------------------------------
FORMAT_TOOLS_DIR := $(PROJ_DIR).format-tools/bin
CLANG_FORMAT_11  := $(shell brew --prefix clang-format@11 2>/dev/null)/bin/clang-format-11

.PHONY: format-fix format-check _format-tools

_format-tools:
	@if [ ! -x "$(CLANG_FORMAT_11)" ]; then \
	  echo "clang-format 11 not found. Install it with: brew install clang-format@11"; \
	  exit 1; \
	fi
	@mkdir -p $(FORMAT_TOOLS_DIR)
	@ln -sf $(CLANG_FORMAT_11) $(FORMAT_TOOLS_DIR)/clang-format
	@printf '#!/bin/sh\necho "cmake-format 0.6.0"\n' > $(FORMAT_TOOLS_DIR)/cmake-format
	@chmod +x $(FORMAT_TOOLS_DIR)/cmake-format

format-fix: _format-tools
	PATH="$(FORMAT_TOOLS_DIR):$$PATH" python3 duckdb/scripts/format.py --all --fix --noconfirm --directories src test

format-check: _format-tools
	PATH="$(FORMAT_TOOLS_DIR):$$PATH" python3 duckdb/scripts/format.py --all --check --directories src test
