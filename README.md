# ggsql DuckDB extension

A DuckDB extension that routes `VISUALISE`/`VISUALIZE` statements through the [ggsql](https://ggsql.org) engine and renders plots with ggsql's native hephaestus renderer. The plot is served from an in-process HTTP server and opened in your default browser — the same renderer that produces the `svg`/`pdf`/`hep` output, so what you see is what you save.

## Building

```sh
git clone --recurse-submodules https://github.com/posit-dev/ggsql-duckdb.git
cd ggsql-duckdb
make
```

The build pulls in two git submodules (`duckdb` and `extension-ci-tools`), so a plain `git clone` leaves them empty and `make` fails with a missing `extension-ci-tools/makefiles/duckdb_extension.Makefile` error. In an existing clone, run `git submodule update --init --recursive` to fix that.

Produces:

- `./build/release/duckdb` — DuckDB shell with the extension statically linked in (ready to use).
- `./build/release/test/unittest` — the test runner, extension linked in.
- `./build/release/extension/ggsql/ggsql.duckdb_extension` — the loadable binary for distribution.

The Rust staticlib (`rust/`) is built automatically by CMake; `cargo` must be on `PATH`. On macOS the extension links against `CoreFoundation`, `Security`, and `SystemConfiguration`.

`vcpkg.json` has no C++ dependencies declared, so local builds don't require a vcpkg toolchain. CI builds do pick it up for overlay ports/triplets shared with the DuckDB extension CI.

## Running the extension

Start the shell with `./build/release/duckdb` — the extension is linked in. Or `LOAD ggsql;` against a regular DuckDB shell that has the loadable `.duckdb_extension` available.

Two surfaces are exposed:

**ParserExtension — type ggsql directly:**
```
D SELECT 1 AS x, 2 AS y VISUALISE x, y DRAW point;
D
```

(No output in silent mode, which is the default. The plot has been served to a browser tab.)

**Scalar function — pass ggsql as a string:**
```
D SELECT ggsql('SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line');
```

Both forms open the default browser on the served URL. Set `GGSQL_NO_OPEN_BROWSER=1` in the environment to suppress the browser open (useful for tests and headless runs).

### Output mode (`ggsql_output`)

Use the session setting `ggsql_output` to choose what a query produces:

| Value | Behaviour |
|---|---|
| `silent` *(default)* | Opens the default browser; the `VISUALISE` statement produces **no result set at all**. Good for interactive use — you see the plot, the shell doesn't spam a URL at you. |
| `url` | Opens the browser and returns the plot URL in a 1×1 result. Good for scripts that want the URL. |
| `spec` | Returns the raw vega-lite JSON as VARCHAR. No HTTP server, no browser. Good for piping to other tools. |
| `html` | Returns a self-contained HTML document (~1.9 MB — the .hep plot document plus the hephaestus wasm viewer and Roboto faces inlined). No HTTP server, no browser. Same rendering as the interactive display, so a saved snapshot looks identical: `COPY (SELECT ggsql('…')) TO 'plot.html'`. |
| `svg` | Returns the plot as SVG text (VARCHAR), rendered natively by ggsql — no vega-lite, no server, no browser. |
| `pdf` | Returns the plot as PDF bytes. Use the table form `ggsql_run('…')`, which types the column as BLOB. |
| `hep` | Returns the plot as a `.hep` plot document (ggsql's native format). BLOB from `ggsql_run('…')`. |

The native writers (`svg`/`pdf`/`hep`) — and the `html` and browser display modes, which render through the same pipeline — take per-session options via `ggsql_writer_options`, a semicolon-separated `key=value` string forwarded to ggsql's writer. Shared keys are `width`, `height`, `units`, `dpi`, and `background`, and each writer adds its own (e.g. `embed-fonts` for `svg`). Unknown keys are rejected with an error naming them. Only `spec` mode rejects options outright (it's raw vega-lite JSON, a different backend kept as an escape hatch).

### Saving to a file (`ggsql_save`)

`ggsql_save(query, path)` renders a query straight to a file; the writer is inferred from the extension (`.svg`, `.pdf`, `.hep`, `.html`, `.json` for the raw vega-lite spec), `ggsql_writer_options` applies, and the `ggsql_output` mode is ignored. It returns the path.

```sql
SET ggsql_writer_options = 'width=800;height=600';
SELECT ggsql_save('SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line', 'plot.svg');
SELECT ggsql_save('SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line', 'plot.pdf');
```

```sql
-- default: just see the plot, no shell output
SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line;

-- get the URL back
SET ggsql_output = 'url';
SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line;

-- get the raw spec
SET ggsql_output = 'spec';
SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line;
-- → { "$schema": "...", "data": {...}, "mark": "line", ... }

-- write a self-contained HTML file to disk
SET ggsql_output = 'html';
COPY (SELECT ggsql('SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line')) TO 'plot.html';

-- render natively to SVG, sized via writer options
SET ggsql_output = 'svg';
SET ggsql_writer_options = 'width=800;height=600';
SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line;

-- get a PDF back as BLOB (use the table form, which types the column as BLOB)
SET ggsql_output = 'pdf';
SET ggsql_writer_options = '';
SELECT plot FROM ggsql_run('SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line');

RESET ggsql_output;  -- back to silent
```

In every non-silent mode the result column is named `plot`, so wrapper queries (`SELECT plot FROM …`) keep working when the mode is toggled.

## Session sharing (current limitation)

ggsql queries execute on a **fresh DuckDB connection** to the same database instance, not on the session that issued the query. This means ggsql can see:

- Tables in attached `.duckdb` files
- `CREATE VIEW` (non-temporary) definitions
- Data under persistent catalogs

...but **not**:

- `CREATE TEMP TABLE` / `CREATE TEMP VIEW` defined in the current shell session
- Per-session `SET` variables
- Relations registered on the outer `Connection` from the client side (e.g. a Python `duckdb.register(...)`)

So this fails to find `flights`:

```sql
CREATE TEMP TABLE flights AS SELECT * FROM 'flights.csv';
SELECT * FROM flights VISUALISE dep_delay, arr_delay DRAW point;  -- ❌ table not found
```

The workaround is to use a regular view (or a real table in an attached DB):

```sql
CREATE OR REPLACE VIEW flights AS SELECT * FROM 'flights.csv';
SELECT * FROM flights VISUALISE dep_delay, arr_delay DRAW point;  -- ✅
```

The reason is structural: calling `Query` back into the outer `ClientContext` from inside an executing table function deadlocks on the context's mutex, so we open a sibling `Connection` — which by DuckDB's design has its own temp catalog.

### Query interruption

Interrupting the calling connection (for example, Python's `connection.interrupt()`)
also interrupts SQL running on ggsql's sibling connection. This applies to both
`SELECT ggsql('...')` and direct `VISUALISE` statements. Cancellation is reported as
a DuckDB interruption error, and the connection can be used for subsequent queries.

The bridge checks for cancellation between SQL calls and Arrow batches, and forwards
the caller's interrupt flag every 10 ms while the inner connection is alive. Actual
termination still depends on the running DuckDB operation reaching a cancellation
check; the bridge cannot preempt code inside a blocking external function or ggsql's
Rust rendering code.

## Running the tests

SQL logic tests under `test/sql/` are the primary test surface:

```sh
GGSQL_NO_OPEN_BROWSER=1 make test
```

`GGSQL_NO_OPEN_BROWSER=1` prevents a browser tab from opening for every test query.

The embedded interruption tests use the Python client to interrupt an active query
from another thread. Install a Python DuckDB version matching the extension build:

```sh
python -m pip install duckdb==1.5.4 numpy
GGSQL_EXTENSION_PATH="$PWD/build/release/extension/ggsql/ggsql.duckdb_extension" \
  python -m unittest discover -s test/python -v
```

These tests cover both entry points, single- and multi-threaded execution, repeated
interruptions, connection reuse, and isolation from unrelated queries. Each scenario runs in a subprocess with a
timeout so a cancellation regression fails instead of hanging the test runner.
