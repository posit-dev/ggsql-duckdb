#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

// A TableFunction wrapping ggsql execution. Takes one VARCHAR (the ggsql query) and
// emits a single-row `plot` result — VARCHAR for text writers (url/spec/html/svg),
// BLOB for binary ones (pdf/hep). Used by the ParserExtension plan_function.
class GgsqlRunTableFunction : public TableFunction {
public:
	GgsqlRunTableFunction();
};

// Entry point for the scalar form `SELECT ggsql('...')`. Kept symmetric with the
// TableFunction path so both surfaces run through the same Rust pipeline.
void GgsqlScalarFun(DataChunk &args, ExpressionState &state, Vector &result);

// Entry point for the save form `SELECT ggsql_save('...', 'path.svg')`. The
// writer is inferred from the file extension (.svg/.pdf/.hep/.html/.json) and
// the file is written with the session's ggsql_writer_options applied.
void GgsqlSaveFun(DataChunk &args, ExpressionState &state, Vector &result);

// Whether the current session's `ggsql_output` setting is 'silent' (the default).
// The parser extension consults this at plan time so it can mark the extension
// statement as returning nothing — that lets DuckDB's shell / API clients skip
// rendering entirely instead of showing a zero-row `plot` result.
bool IsSilentOutputMode(ClientContext &context);

} // namespace duckdb
