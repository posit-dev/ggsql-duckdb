cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME ggsql)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

include_directories(
  src/include
  rust/include
)

set(EXTENSION_SOURCES
  src/ggsql_extension.cpp
  src/ggsql_bridge.cpp
  src/ggsql_parser.cpp
  src/ggsql_exec.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# -----------------------------------------------------------------------------
# Rust side (ggsql_ext_rs staticlib — embeds ggsql + the vega-lite HTTP server)
# -----------------------------------------------------------------------------
set(GGSQL_RUST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rust)
set(GGSQL_RUST_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/rust-target)

find_program(CARGO_EXECUTABLE cargo REQUIRED)

# extension-ci-tools' Makefile passes -DRust_CARGO_TARGET=<triple> for
# macOS cross-builds and Windows mingw (matching the Corrosion convention).
# Honor it: pass --target to cargo and adjust the output path accordingly.
# Without --target, cargo builds for the host arch and the final link fails on
# osx_amd64 (arm64 runner → x86_64 binary) with "ignoring file ... found
# architecture arm64, required architecture x86_64".
if(Rust_CARGO_TARGET)
  set(GGSQL_CARGO_TARGET_ARGS --target ${Rust_CARGO_TARGET})
  set(GGSQL_RUST_LIB ${GGSQL_RUST_BUILD_DIR}/${Rust_CARGO_TARGET}/release/libggsql_ext_rs.a)
else()
  set(GGSQL_CARGO_TARGET_ARGS "")
  set(GGSQL_RUST_LIB ${GGSQL_RUST_BUILD_DIR}/release/libggsql_ext_rs.a)
endif()

# Cargo's stamp file is unreliable; list the Rust sources as the dependency set.
file(GLOB_RECURSE GGSQL_RUST_SOURCES
  ${GGSQL_RUST_DIR}/src/*.rs
  ${GGSQL_RUST_DIR}/include/*.h
  ${GGSQL_RUST_DIR}/assets/*.js
  ${GGSQL_RUST_DIR}/assets/*.html
  ${GGSQL_RUST_DIR}/Cargo.toml
)

# tree-sitter-ggsql's build.rs re-runs `tree-sitter generate` from grammar.js
# unless GGSQL_SKIP_GENERATE=1; the published crate already ships the
# generated src/parser.c, so setting this avoids a hard dep on the tree-sitter
# CLI in CI and clean builds.
add_custom_command(
  OUTPUT ${GGSQL_RUST_LIB}
  COMMAND ${CMAKE_COMMAND} -E env GGSQL_SKIP_GENERATE=1
          ${CARGO_EXECUTABLE} build --release
          --manifest-path ${GGSQL_RUST_DIR}/Cargo.toml
          --target-dir ${GGSQL_RUST_BUILD_DIR}
          ${GGSQL_CARGO_TARGET_ARGS}
  DEPENDS ${GGSQL_RUST_SOURCES}
  WORKING_DIRECTORY ${GGSQL_RUST_DIR}
  COMMENT "Building ggsql_ext_rs (release)"
  VERBATIM
)

add_custom_target(ggsql_rust_lib DEPENDS ${GGSQL_RUST_LIB})

# macOS needs CoreFoundation + Security for some Rust std bits (tls, tiny_http).
if(APPLE)
  set(GGSQL_RUST_SYSDEPS "-framework CoreFoundation" "-framework Security" "-framework SystemConfiguration")
elseif(UNIX)
  set(GGSQL_RUST_SYSDEPS pthread dl m)
else()
  set(GGSQL_RUST_SYSDEPS "")
endif()

foreach(target ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME})
  add_dependencies(${target} ggsql_rust_lib)
  target_link_libraries(${target} ${GGSQL_RUST_LIB} ${GGSQL_RUST_SYSDEPS})
endforeach()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
)
