#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

// A TableFunction wrapping ggsql execution. Takes one VARCHAR (the ggsql query) and
// emits a single-row VARCHAR plot_url result. Used by the ParserExtension plan_function.
class GgsqlRunTableFunction : public TableFunction {
public:
	GgsqlRunTableFunction();
};

// Entry point for the scalar form `SELECT ggsql('...')`. Kept symmetric with the
// TableFunction path so both surfaces run through the same Rust pipeline.
void GgsqlScalarFun(DataChunk &args, ExpressionState &state, Vector &result);

// Whether the current session's `ggsql_output` setting is 'silent' (the default).
// The parser extension consults this at plan time so it can mark the extension
// statement as returning nothing — that lets DuckDB's shell / API clients skip
// rendering entirely instead of showing a zero-row `plot` result.
bool IsSilentOutputMode(ClientContext &context);

} // namespace duckdb
