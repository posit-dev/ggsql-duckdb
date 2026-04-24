# This file is included by DuckDB's build system. It specifies which extension to load

# Derive EXTENSION_VERSION from the git tag so the release flow has a single
# source of truth. On a tagged commit: returns the tag (e.g. v0.1.0). On a
# later commit: v0.1.0-3-gabc1234. Dirty tree: ...-dirty. No tags / no git:
# falls back to v0.0.0-dev. community-extensions CI checks out with
# fetch-depth: 0 and fetch-tags: true, so the tag is always resolvable there.
execute_process(
    COMMAND git describe --tags --dirty
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GGSQL_EXTENSION_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE GGSQL_GIT_DESCRIBE_RC
    ERROR_QUIET
)
if(NOT GGSQL_GIT_DESCRIBE_RC EQUAL 0 OR GGSQL_EXTENSION_VERSION STREQUAL "")
    set(GGSQL_EXTENSION_VERSION "v0.0.0-dev")
endif()

# Extension from this repo
duckdb_extension_load(ggsql
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
    EXTENSION_VERSION ${GGSQL_EXTENSION_VERSION}
)

# Any extra extensions that should be built
# e.g.: duckdb_extension_load(json)
