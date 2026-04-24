# ggsql-duckdb

A DuckDB extension that routes `VISUALISE`/`VISUALIZE` statements through the [ggsql](https://ggsql.org) engine and renders vega-lite charts. The chart is served from an in-process HTTP server and opened in your default browser.

## Building

```sh
make
```

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
D SELECT ggsql('SELECT * FROM range(10) t(x) VISUALISE x, x*x AS y DRAW line');
```

Both forms open the default browser on the served URL. Set `GGSQL_NO_OPEN_BROWSER=1` in the environment to suppress the browser open (useful for tests and headless runs).

### Output mode (`ggsql_output`)

Use the session setting `ggsql_output` to choose what a query produces:

| Value | Behaviour |
|---|---|
| `silent` *(default)* | Opens the default browser; the `VISUALISE` statement produces **no result set at all**. Good for interactive use — you see the plot, the shell doesn't spam a URL at you. |
| `url` | Opens the browser and returns the plot URL in a 1×1 result. Good for scripts that want the URL. |
| `spec` | Returns the raw vega-lite JSON as VARCHAR. No HTTP server, no browser. Good for piping to other tools. |
| `html` | Returns a self-contained HTML document (~830 KB — vega + vega-lite + vega-embed inlined from the vendored bundles, plus the spec). No HTTP server, no browser. Good for saving a shareable snapshot: `COPY (SELECT ggsql('…')) TO 'plot.html'`. |

```sql
-- default: just see the plot, no shell output
SELECT * FROM range(10) t(x) VISUALISE x, x*x AS y DRAW line;

-- get the URL back
SET ggsql_output = 'url';
SELECT * FROM range(10) t(x) VISUALISE x, x*x AS y DRAW line;

-- get the raw spec
SET ggsql_output = 'spec';
SELECT * FROM range(10) t(x) VISUALISE x, x*x AS y DRAW line;
-- → { "$schema": "...", "data": {...}, "mark": "line", ... }

-- write a self-contained HTML file to disk
SET ggsql_output = 'html';
COPY (SELECT ggsql('SELECT * FROM range(10) t(x) VISUALISE x, x*x AS y DRAW line')) TO 'plot.html';

RESET ggsql_output;  -- back to silent
```

In `url`/`spec`/`html` modes the result column is named `plot`, so wrapper queries (`SELECT plot FROM …`) keep working when the mode is toggled.

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

The reason is structural: calling `Query` back into the outer `ClientContext` from inside an executing table function deadlocks on the context's mutex, so we open a sibling `Connection` — which by DuckDB's design has its own temp catalog. We intend to revisit this once ggsql's engine stops requiring recursive SQL callbacks (tracked upstream).

## Running the tests

SQL logic tests under `test/sql/` are the primary test surface:

```sh
GGSQL_NO_OPEN_BROWSER=1 make test
```

`GGSQL_NO_OPEN_BROWSER=1` prevents a browser tab from opening for every test query.
