#pragma once

#include "duckdb.hpp"
#include "duckdb/parser/parser_extension.hpp"

namespace duckdb {

// Data stashed between parse_function and plan_function — just the original ggsql query.
struct GgsqlParseData : public ParserExtensionParseData {
	explicit GgsqlParseData(string query_p) : query(std::move(query_p)) {
	}

	string query;

	duckdb::unique_ptr<ParserExtensionParseData> Copy() const override {
		return make_uniq<GgsqlParseData>(query);
	}

	string ToString() const override {
		return query;
	}
};

// A ParserExtension that claims any statement containing a top-level
// VISUALISE/VISUALIZE keyword. Everything else is declined so DuckDB's core parser
// handles it.
class GgsqlParserExtension : public ParserExtension {
public:
	GgsqlParserExtension();
};

// Scanner exposed for testability. Returns true if `query` contains a top-level
// VISUALISE/VISUALIZE keyword, skipping string literals, quoted identifiers, and
// SQL line/block comments.
bool ContainsVisualiseKeyword(const string &query);

} // namespace duckdb
