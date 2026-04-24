# Ggsql

This repository is based on https://github.com/duckdb/extension-template, check it out if you want to build and ship your own DuckDB extension.

---

This extension lets you issue [ggsql](https://ggsql.org) visualisation queries directly inside a DuckDB session. When loaded, any statement containing a top-level `VISUALISE`/`VISUALIZE` keyword is routed through the ggsql engine; the resulting vega-lite chart is served from an in-process HTTP server and opened in your default browser. The shell returns a `plot_url` row pointing at the served spec.


## Building
### Managing dependencies
DuckDB extensions uses VCPKG for dependency management. Enabling VCPKG is very simple: follow the [installation instructions](https://vcpkg.io/en/getting-started) or just run the following:
```shell
git clone https://github.com/Microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh
export VCPKG_TOOLCHAIN_PATH=`pwd`/vcpkg/scripts/buildsystems/vcpkg.cmake
```
Note: VCPKG is only required for extensions that want to rely on it for dependency management. If you want to develop an extension without dependencies, or want to do your own dependency management, just skip this step. Note that the example extension uses VCPKG to build with a dependency for instructive purposes, so when skipping this step the build may not work without removing the dependency.

### Build steps
Now to build the extension, run:
```sh
make
```
The main binaries that will be built are:
```sh
./build/release/duckdb
./build/release/test/unittest
./build/release/extension/ggsql/ggsql.duckdb_extension
```
- `duckdb` is the binary for the duckdb shell with the extension code automatically loaded.
- `unittest` is the test runner of duckdb. Again, the extension is already linked into the binary.
- `ggsql.duckdb_extension` is the loadable binary as it would be distributed.

## Running the extension
Start the shell with `./build/release/duckdb` (the extension is linked in for the dev build) or `LOAD ggsql;` against a regular DuckDB shell that has the loadable `.duckdb_extension` available.

Two surfaces are exposed:

**ParserExtension — type ggsql directly:**
```
D SELECT 1 AS x, 2 AS y VISUALISE x, y DRAW point;
┌──────────────────────────────────────────────────────────────────┐
│                             plot_url                             │
├──────────────────────────────────────────────────────────────────┤
│ http://127.0.0.1:<port>/plot/<uuid>                              │
└──────────────────────────────────────────────────────────────────┘
```

**Scalar function — pass ggsql as a string:**
```
D SELECT ggsql('SELECT * FROM range(10) t(x) VISUALISE x, x*x AS y DRAW line');
```

Both forms open the default browser on the URL. Set `GGSQL_NO_OPEN_BROWSER=1` in the environment to suppress the browser open (useful for tests and headless runs).

### Output mode (`ggsql_output`)
Use the session setting `ggsql_output` to choose what the result column contains:

| Value | Behaviour |
|---|---|
| `silent` *(default)* | Opens the default browser; the SQL result has **zero rows**. Good for interactive use — you see the plot, the shell doesn't spam a URL at you. |
| `url` | Opens the browser and returns the plot URL in a 1×1 result. Good for scripts that want the URL. |
| `spec` | Returns the raw vega-lite JSON as VARCHAR. No HTTP server, no browser. Good for piping to other tools. |
| `html` | Returns a self-contained HTML document (~830 KB — vega + vega-lite + vega-embed inlined from the vendored bundles, plus the spec). No HTTP server, no browser. Good for saving a shareable snapshot: `COPY (SELECT ggsql('…')) TO 'plot.html'`. |

```sql
-- default: just see the plot
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

The result column is always named `plot` regardless of mode, so wrapper queries (`SELECT plot FROM …`) keep working when the mode is toggled.

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
Different tests can be created for DuckDB extensions. The primary way of testing DuckDB extensions should be the SQL tests in `./test/sql`. These SQL tests can be run using:
```sh
make test
```

### Installing the deployed binaries
To install your extension binaries from S3, you will need to do two things. Firstly, DuckDB should be launched with the
`allow_unsigned_extensions` option set to true. How to set this will depend on the client you're using. Some examples:

CLI:
```shell
duckdb -unsigned
```

Python:
```python
con = duckdb.connect(':memory:', config={'allow_unsigned_extensions' : 'true'})
```

NodeJS:
```js
db = new duckdb.Database(':memory:', {"allow_unsigned_extensions": "true"});
```

Secondly, you will need to set the repository endpoint in DuckDB to the HTTP url of your bucket + version of the extension
you want to install. To do this run the following SQL query in DuckDB:
```sql
SET custom_extension_repository='bucket.s3.eu-west-1.amazonaws.com/<your_extension_name>/latest';
```
Note that the `/latest` path will allow you to install the latest extension version available for your current version of
DuckDB. To specify a specific version, you can pass the version instead.

After running these steps, you can install and load your extension using the regular INSTALL/LOAD commands in DuckDB:
```sql
INSTALL ggsql;
LOAD ggsql;
```

## Setting up CLion

### Opening project
Configuring CLion with this extension requires a little work. Firstly, make sure that the DuckDB submodule is available.
Then make sure to open `./duckdb/CMakeLists.txt` (so not the top level `CMakeLists.txt` file from this repo) as a project in CLion.
Now to fix your project path go to `tools->CMake->Change Project Root`([docs](https://www.jetbrains.com/help/clion/change-project-root-directory.html)) to set the project root to the root dir of this repo.

### Debugging
To set up debugging in CLion, there are two simple steps required. Firstly, in `CLion -> Settings / Preferences -> Build, Execution, Deploy -> CMake` you will need to add the desired builds (e.g. Debug, Release, RelDebug, etc). There's different ways to configure this, but the easiest is to leave all empty, except the `build path`, which needs to be set to `../build/{build type}`, and CMake Options to which the following flag should be added, with the path to the extension CMakeList:

```
-DDUCKDB_EXTENSION_CONFIGS=<path_to_the_exentension_CMakeLists.txt>
```

The second step is to configure the unittest runner as a run/debug configuration. To do this, go to `Run -> Edit Configurations` and click `+ -> Cmake Application`. The target and executable should be `unittest`. This will run all the DuckDB tests. To specify only running the extension specific tests, add `--test-dir ../../.. [sql]` to the `Program Arguments`. Note that it is recommended to use the `unittest` executable for testing/development within CLion. The actual DuckDB CLI currently does not reliably work as a run target in CLion.
