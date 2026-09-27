#include "ggsql_parser.hpp"
#include "ggsql_exec.hpp"

#include "duckdb/common/enums/statement_type.hpp"
#include "duckdb/common/numeric_utils.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"

namespace duckdb {

namespace {

bool IsEndOfStatement(const SimpleToken &token) {
	return token.type == TokenType::TERMINATOR || token.type == TokenType::END_OF_INPUT ||
	       token.type == TokenType::END_OF_INPUT_AUTOCOMPLETE;
}

bool ContainsVisualiseKeyword(const vector<SimpleToken> &tokens, idx_t statement_size) {
	idx_t depth = 0;
	for (idx_t i = 0; i < statement_size; i++) {
		auto &token = tokens[i];
		if (token.type == TokenType::OPERATOR) {
			if (token.text == "(" || token.text == "[" || token.text == "{") {
				depth++;
			} else if ((token.text == ")" || token.text == "]" || token.text == "}") && depth > 0) {
				depth--;
			}
			continue;
		}
		// Failed PEG parsing may have refined an identifier's type (for example,
		// to COLUMN_NAME). Match its raw spelling regardless of that annotation.
		// Quoted identifiers and literals retain their delimiters and cannot match.
		if (depth == 0 && token.type != TokenType::COMMENT &&
		    (StringUtil::CIEquals(token.text, "VISUALISE") || StringUtil::CIEquals(token.text, "VISUALIZE"))) {
			return true;
		}
	}
	return false;
}

string ReconstructQuery(const vector<SimpleToken> &tokens, idx_t statement_size) {
	string query;
	optional_ptr<const SimpleToken> previous;
	for (idx_t i = 0; i < statement_size; i++) {
		auto &token = tokens[i];
		if (token.type == TokenType::COMMENT) {
			continue;
		}
		if (previous) {
			// DuckDB 2.0 supplies raw token spellings but no source offsets or
			// whitespace. Most ggsql rules allow whitespace between tokens; its
			// namespaced_identifier, number, and infinity rules are lexical tokens
			// and require these pieces to remain adjacent.
			auto namespace_separator = token.text == ":" || previous->text == ":";
			auto signed_literal = (previous->text == "-" || previous->text == "+") &&
			                      (token.type == TokenType::NUMBER_LITERAL || StringUtil::CIEquals(token.text, "Inf"));
			if (!namespace_separator && !signed_literal) {
				// SQL permits adjacent string literals only when separated by a
				// newline. A space would invalidate that supported SQL spelling.
				query +=
				    previous->type == TokenType::STRING_LITERAL && token.type == TokenType::STRING_LITERAL ? '\n' : ' ';
			}
		}
		query += token.text;
		previous = &token;
	}
	return query;
}

ParserExtensionParseResult ParseFunction(ParserExtensionInfo *, const vector<SimpleToken> &tokens) {
	// The callback receives the entire unparsed tail, which can contain later
	// SQL and ggsql statements. Claim only this statement; a semicolon inside a
	// quoted literal is part of that literal, not a TERMINATOR token.
	idx_t statement_size = 0;
	while (statement_size < tokens.size() && !IsEndOfStatement(tokens[statement_size])) {
		statement_size++;
	}
	if (!ContainsVisualiseKeyword(tokens, statement_size)) {
		return ParserExtensionParseResult();
	}

	auto result = ParserExtensionParseResult(make_uniq<GgsqlParseData>(ReconstructQuery(tokens, statement_size)));
	// Own the terminator (including DuckDB's end-of-input sentinel) without
	// forwarding it to ggsql's tree-sitter grammar.
	auto consumed_tokens = statement_size;
	if (statement_size < tokens.size()) {
		consumed_tokens++;
	}
	result.consumed_tokens = NumericCast<int64_t>(consumed_tokens);
	return result;
}

ParserExtensionPlanResult PlanFunction(ParserExtensionInfo *, ClientContext &context,
                                       duckdb::unique_ptr<ParserExtensionParseData> parse_data) {
	auto &data = parse_data->Cast<GgsqlParseData>();

	ParserExtensionPlanResult result;
	result.function = GgsqlRunTableFunction();
	result.parameters.push_back(Value(data.query));
	result.requires_valid_transaction = false;
	// Marking the return type as NOTHING lets API clients suppress the result
	// while the table function still performs the server/browser side effect.
	result.return_type = IsSilentOutputMode(context) ? StatementReturnType::NOTHING : StatementReturnType::QUERY_RESULT;
	return result;
}

} // namespace

GgsqlParserExtension::GgsqlParserExtension() {
	parse_function = ParseFunction;
	plan_function = PlanFunction;
}

} // namespace duckdb
