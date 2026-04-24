# ggsql-duckdb

DuckDB extension that routes `VISUALISE`/`VISUALIZE` statements through the Rust [ggsql](https://ggsql.org) engine, rendering vega-lite charts. Built on the DuckDB extension template.

## Layout

```
src/                    C++ extension: ParserExtension, scalar+table funcs, FFI bridge
  ggsql_extension.cpp   Entry point; registers scalar `ggsql()`, parser ext, `ggsql_output` setting
  ggsql_parser.cpp      Scans for top-level VISUALISE/VISUALIZE keyword; strips trailing `;`
  ggsql_exec.cpp        TableFunction + scalar impls; resolves output mode; calls into Rust
  ggsql_bridge.cpp      FFI callbacks: exec_sql + register_df (temp-table materialisation)
  include/*.hpp
rust/                   Rust staticlib linked into the extension
  src/lib.rs            `ggsql_execute` C entrypoint; dispatches on output mode
  src/reader.rs         `CallbackReader` impls ggsql::Reader via the C++ bridge
  src/server.rs         tiny_http singleton serving the SPA + vega assets
  src/ffi.rs            C ABI types (ByteBuffer, ReaderBridge)
  src/dialect.rs        DuckDbDialect, inlined from ggsql (see comment below)
  include/ggsql_ext_rs.h  Hand-rolled C header; must track ffi.rs
  assets/               Vendored vega/vega-lite/vega-embed bundles + SPA shell
test/sql/ggsql.test     SQL logic tests — run with GGSQL_NO_OPEN_BROWSER=1
duckdb/, extension-ci-tools/  Submodules; versions bumped per release (see docs/UPDATING.md)
```

## Build / test

- `make` — builds everything; `./build/release/duckdb` has the extension linked in.
- `make test` — runs SQL logic tests. Set `GGSQL_NO_OPEN_BROWSER=1` to stop Rust from launching a browser tab per test.
- CMake drives `cargo build --release` via a custom command; Rust sources + `assets/*` are listed as dependencies so edits trigger rebuilds.
- macOS links `CoreFoundation`, `Security`, `SystemConfiguration` (needed by Rust std / tiny_http TLS bits).
- DuckDB target version is set in three places — keep them in sync: `.github/workflows/MainDistributionPipeline.yml` (`duckdb_version`, `ci_tools_version`, workflow `@` tag) and the `duckdb` + `extension-ci-tools` submodule refs. `docs/UPDATING.md` has the full checklist.

## User-visible surface

Two entry points, both funnel into the same Rust `ggsql_execute`:

- **ParserExtension** — any statement containing a top-level `VISUALISE`/`VISUALIZE` keyword is claimed and planned as a call to the `ggsql_run` table function. The scanner in `ggsql_parser.cpp` is hand-rolled: it skips `'...'`, `"..."`, `--` line comments and `/* */` block comments, and matches only at word boundaries. A trailing `;` is stripped because ggsql's tree-sitter grammar rejects it.
- **Scalar** — `SELECT ggsql('<query>')` runs the same pipeline with the string as input.

Output mode is session-scoped via the `ggsql_output` setting (`silent` default / `url` / `spec` / `html`). The result column is always named `plot` — don't rename it. Unknown values throw at bind time. `silent` emits zero rows; `url` emits one; `spec`/`html` return the bytes and do not start the HTTP server or open a browser.

## FFI contract (C++ ↔ Rust)

- `ggsql_execute(query, len, bridge, mode, out) -> int32` — 0 ok, 1 error (payload in `out`), 2 panic. `out` is a `ggsql_byte_buffer_t` allocated by Rust; **caller must free via `ggsql_free_buffer`**.
- `ReaderBridge` has three callbacks, all implemented in `ggsql_bridge.cpp`:
  - `exec_sql` — runs SQL on the inner `Connection`, returns Arrow stream via C Data Interface.
  - `register_df` — accepts an Arrow stream from Rust and materialises it as a `TEMP TABLE` (via `arrow_scan`). Used as engine-internal staging by ggsql 0.2.7.
  - `free_buffer` — C++ frees a buffer it previously populated (for error messages).
- `ffi.rs` and `rust/include/ggsql_ext_rs.h` **must stay layout-compatible**. The header is hand-written, not cbindgen'd.
- `OwnedArrowStreamFactory` in `ggsql_bridge.cpp` relies on `ArrowArrayStream` being the first field so `reinterpret_cast` works — do not reorder.

## Inner-Connection model (critical, surprising)

Every ggsql call opens a **sibling `Connection` on the same `DatabaseInstance`** (`BridgeCtx::inner`), not the session that issued the query. This is why:

- Temp tables / temp views / per-session `SET` / client-registered relations in the user's session are **invisible** to ggsql.
- Persistent views, attached DBs, and real tables **are** visible.
- Users working around this create `CREATE OR REPLACE VIEW` instead of `CREATE TEMP TABLE`.

**Reason**: calling `ClientContext::Query` re-entrantly from inside an executing table function deadlocks on the context's mutex. We open a sibling `Connection` to sidestep it. This is documented in the README under "Session sharing (current limitation)". Revisit only when ggsql stops requiring recursive SQL callbacks.

The inner `Connection` is created lazily and **persists for the whole `ggsql_execute` call**, so temp tables that `register_df` creates remain visible to subsequent `exec_sql` calls within the same invocation.

## HTTP server

- `tiny_http` bound to `127.0.0.1:0` (random port), stored in a `OnceCell` — one server for the process lifetime.
- Spec registry is a `HashMap<uuid, json>` in memory; unbounded (grows for the life of the process).
- SPA URL form is `http://.../#plot/<uuid>` — the hash matters: browsers treat tabs with different fragments as the same URL for `open::that`, so repeat queries focus the existing tab instead of spawning new ones.
- `/api/latest` doubles as a liveness heartbeat: if it was polled within `TAB_ALIVE_WINDOW` (5s), `register_spec` sets `should_open=false` and the poll loop in the SPA picks up the new plot via `history.pushState` — no second window. `GGSQL_NO_OPEN_BROWSER` overrides regardless.
- Vega/vega-lite/vega-embed bundles are `include_str!`'d from `rust/assets/` so plots render offline. `html` mode inlines the same bundles into a single self-contained document. `</` in the spec is escaped to avoid `</script>` breakout.

## Transitional pieces (watch for `TODO(ggsql-*)`)

These will go away with the next ggsql release (main branch has already migrated to arrow):

- `rust/src/reader.rs` — `exec_sql_via_bridge` does an Arrow → IPC bytes → polars IPC roundtrip because ggsql 0.2.7's `Reader` trait still returns `polars::DataFrame`. Tagged `TODO(ggsql-arrow-migration)`.
- `rust/src/reader.rs::register` — ggsql 0.2.7's engine materialises the main SELECT as `__ggsql_global_<uuid>__` via `register`. The whole `register_df` FFI + `OwnedArrowStreamFactory` plumbing in `ggsql_bridge.cpp` can be deleted when that's gone. Tagged `TODO(ggsql-register-regression)`.
- `rust/Cargo.toml` — the `polars` dep is transitional; drop alongside the reader migration.
- `rust/src/dialect.rs` — `DuckDbDialect` is **inlined from ggsql 0.2.7** (`src/reader/duckdb.rs`). We can't enable ggsql's `duckdb` feature because it pulls in `duckdb-rs` with `bundled`, which would statically link a second DuckDB into an extension already loaded inside DuckDB (symbol clashes + binary bloat). If upstream changes the dialect, re-sync manually.

## Conventions

- C++ formatting follows `duckdb/.clang-format` (symlinked at repo root and in `ggsql-duckdb/`). Linting via `clang-tidy`. CI runs `format;tidy` checks.
- SQL logic tests are the primary test surface — prefer adding coverage there over adding new test harnesses.
- Comments in C++/Rust lean toward explaining **why** (invariants, FFI contracts, upstream coupling). Don't strip them without understanding — several encode hazards (mutex deadlock, struct layout, `</script>` breakout, `open::that` tab-matching behaviour).
