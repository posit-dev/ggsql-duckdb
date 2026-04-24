// C ABI for ggsql_ext_rs. Hand-rolled (not cbindgen'd) — layouts must track
// rust/src/ffi.rs.

#pragma once

#include <stddef.h>
#include <stdint.h>

// Forward-declare the Arrow C Stream Interface struct. If the caller has already
// included Arrow's C data interface header (or duckdb/common/arrow/arrow.hpp, which
// defines the same struct via the ARROW_FLAG_DICTIONARY_ORDERED guard), that
// definition is the canonical one — this just declares the tag.
#ifdef __cplusplus
extern "C" {
#endif
struct ArrowArrayStream;
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ggsql_byte_buffer {
    uint8_t *ptr;
    size_t   len;
    size_t   cap;
} ggsql_byte_buffer_t;

// C++ → Rust: execute a SQL statement on the active DuckDB ClientContext. On success
// (return 0), `out_stream` is populated as an Arrow C Data Interface stream owned by
// Rust — Rust will invoke the stream's `release` callback when done. On failure
// (non-zero return), `out_err` holds a UTF-8 error message; Rust releases it via the
// bridge's `free_buffer`.
typedef int32_t (*ggsql_exec_sql_fn)(
    void                   *ctx,
    const char             *sql,
    size_t                  sql_len,
    struct ArrowArrayStream *out_stream,
    ggsql_byte_buffer_t    *out_err);

// Rust → C++: register an Arrow stream as a TEMP TABLE on the C++ inner Connection.
// C++ takes ownership of `stream` (consumes and releases it); Rust must not touch it
// afterward. On failure (non-zero return), `out_err` holds a UTF-8 error message.
// Transitional: required by ggsql 0.2.7's engine, will go away with the next ggsql
// release (see ggsql_polars_migration memory).
typedef int32_t (*ggsql_register_df_fn)(
    void                    *ctx,
    const char              *name,
    size_t                   name_len,
    struct ArrowArrayStream *stream,
    int                      replace,
    ggsql_byte_buffer_t     *out_err);

// C++ frees a buffer it previously populated (called by Rust to release error buffers).
typedef void (*ggsql_free_buffer_fn)(ggsql_byte_buffer_t *buf);

typedef struct ggsql_reader_bridge {
    void                 *ctx;
    ggsql_exec_sql_fn     exec_sql;
    ggsql_register_df_fn  register_df;
    ggsql_free_buffer_fn  free_buffer;
} ggsql_reader_bridge_t;

// Run a ggsql query end-to-end.
//   query      : UTF-8 ggsql source (not required to be NUL-terminated)
//   query_len  : byte length of query
//   bridge     : initialised bridge; stays valid for the call
//   out        : filled with URL (success, returns 0) or error message (returns != 0).
//                Caller must release via ggsql_free_buffer.
int32_t ggsql_execute(
    const char                  *query,
    size_t                       query_len,
    const ggsql_reader_bridge_t *bridge,
    ggsql_byte_buffer_t         *out);

// Free a buffer that Rust populated. Safe to call on zero-initialised buffers.
void ggsql_free_buffer(ggsql_byte_buffer_t *buf);

#ifdef __cplusplus
}
#endif
