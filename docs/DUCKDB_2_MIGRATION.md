# DuckDB 2.0 migration

This branch starts the migration from DuckDB 1.5.4 and ggsql 0.4.1 to the
DuckDB 2.0 alpha API and ggsql 0.5.2. It does not claim compatibility with an
unreleased final DuckDB 2.0 ABI or publish an update to the community repository.

## Reproducible dependencies

| Dependency | Pin |
| --- | --- |
| DuckDB | `d8a1bd4f4fbccdf3d22d8f1fe27a94c11c805d0a` from `v2.0-cyanoptera` |
| extension-ci-tools | `20bad04ce694ea3f4052efb51c1a177dc9891bfd` |
| ggsql | `=0.5.2`, latest published non-yanked release checked on 2026-09-26 |
| Arrow Rust | Major 58, compatible with ggsql 0.5.2; exact resolution in `rust/Cargo.lock` |

The submodule commits and CI workflow refs must stay aligned. The CMake build
uses `cargo build --locked` and rebuilds the Rust library when `Cargo.lock`
changes. Updating the upstream `v2.0-cyanoptera` branch does not silently change
this checkout's pinned dependency.

## Compatibility changes

- Migrate the parser callback from SQL text to DuckDB 2.0's `SimpleToken` API,
  including reporting the exact number of tokens consumed for one statement.
- Reconstruct source while preserving ggsql's namespaced datasets and signed
  numeric literals. Native tokens omit whitespace and comments; the scalar
  `ggsql('<source>')` entry point continues to receive the original query string.
- Use `Identifier` for table-function output names, retaining the `plot` column.
- Mark the scalar function fallible and volatile: query errors are ordinary
  runtime errors, and prepared calls observe current data and output settings.
- Keep the C++ Arrow-stream bridge and sibling connection model. Session-local
  temporary tables and registered relations remain unavailable; this migration
  does not change that documented limitation.
- Update ggsql and the bundled Vega renderer to agree on the Vega-Lite schema.
  The inlined DuckDB dialect is checked against the pinned ggsql release.
- Use the current extension CI tooling and its portable formatter targets.
  Distribution tests set `GGSQL_NO_OPEN_BROWSER=1`.

Direct `VISUALISE` statements pass through DuckDB's tokenizer. Use SQL `--` or
`/* ... */` comments and double-quoted identifiers in that form. For ggsql-only
spellings such as `//` line comments or backtick-quoted identifiers, use the
scalar `ggsql('<source>')` entry point so the ggsql parser receives the source
without token normalization.

One upstream ggsql 0.5.2 edge case remains: a SQL string literal containing `--`
(for example `'semi;colon -- VISUALISE'`) is rejected by the ggsql parser in both
the direct and scalar entry points. The migration tests cover embedded
semicolons and keywords separately from that unsupported comment-marker case.

## Build and validation

Prerequisites: a C++ compiler, CMake 3.14 or newer, Make (optionally Ninja),
Python, and a Rust toolchain capable of building the locked dependencies.

```sh
git submodule update --init --recursive
make
GGSQL_NO_OPEN_BROWSER=1 make test
GGSQL_SKIP_GENERATE=1 cargo check --locked --manifest-path rust/Cargo.toml
```

The SQL suites cover output modes, normal error recovery, prepared scalar
execution, committed in-memory data through the Arrow bridge, CTEs, parser
token boundaries, quoted text, dataset namespaces, and signed literals.
CI builds the supported native platforms; WebAssembly remains excluded because
the existing HTTP server, threading, and browser-launch dependencies require a
native host.

### Validation performed on 2026-09-26

- Linux x86-64 release build completed for DuckDB, the shell, the test runner,
  and the loadable extension using GCC 15 and Rust 1.98.1.
- After the final parser fix, the rebuilt loadable binary passed all three SQL
  suites: 63 assertions (19 core, 17 embedding, 27 parser). Each fresh runner
  database checked that ggsql was unloaded, loaded the artifact by its full path,
  and checked that it was loaded. Temporary copies replaced `require ggsql` with
  explicit `LOAD` so these checks exercised the final dynamic library.
- A separate dynamic-load smoke test passed seven assertions.
- `cargo check --locked` passed; the dependency tree contains no second DuckDB.
- The actual ggsql line-and-point specification compiled with the bundled Vega
  stack and rendered to SVG. Its standalone HTML also rendered in a browser
  without JavaScript warnings or errors.
- C++/SQL/CMake formatting checks and `git diff --check` passed. GitHub's format
  and clang-tidy checks passed during the migration; CI reruns on the final push.

Native Windows/macOS builds and the final DuckDB 2.0 release remain unverified.

## Before release

- Rebase the pins onto the intended final DuckDB 2.0 release and rerun all builds.
- Validate Windows and macOS in CI in addition to local Linux testing.
- Smoke-test browser rendering and self-contained HTML using the bundled assets.
- For community alpha testing, submit this extension commit as `repo.ref_next`
  in the community descriptor; do not replace the stable `repo.ref` prematurely.
- Publish only after those checks pass. No community submission is made by this
  migration branch.

References: [DuckDB 2.0 alpha announcement](https://duckdb.org/2026/09/02/try-duckdb-20-alpha),
[ggsql 0.5.2](https://crates.io/crates/ggsql/0.5.2).
