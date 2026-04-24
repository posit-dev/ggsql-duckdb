#include "ggsql_parser.hpp"
#include "ggsql_exec.hpp"

#include "duckdb/common/enums/statement_type.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"

#include <cctype>
#include <cstring>

namespace duckdb {

namespace {

bool IsIdentChar(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Returns true iff `haystack` at offset `pos` matches `needle` (case-insensitive) and
// the match is bounded on both sides by non-identifier characters (or string edges).
bool MatchesWholeWord(const string &haystack, size_t pos, const char *needle, size_t needle_len) {
	if (pos + needle_len > haystack.size()) {
		return false;
	}
	for (size_t i = 0; i < needle_len; i++) {
		char a = haystack[pos + i];
		char b = needle[i];
		if (std::tolower(static_cast<unsigned char>(a)) != std::tolower(static_cast<unsigned char>(b))) {
			return false;
		}
	}
	if (pos > 0 && IsIdentChar(haystack[pos - 1])) {
		return false;
	}
	size_t after = pos + needle_len;
	if (after < haystack.size() && IsIdentChar(haystack[after])) {
		return false;
	}
	return true;
}

static const char *VISUALISE_KEYWORDS[] = {"VISUALISE", "VISUALIZE"};

} // namespace

bool ContainsVisualiseKeyword(const string &query) {
	size_t i = 0;
	const size_t n = query.size();
	while (i < n) {
		char c = query[i];
		// Line comment
		if (c == '-' && i + 1 < n && query[i + 1] == '-') {
			i += 2;
			while (i < n && query[i] != '\n') {
				i++;
			}
			continue;
		}
		// Block comment (SQL standard: not nestable — keep it simple)
		if (c == '/' && i + 1 < n && query[i + 1] == '*') {
			i += 2;
			while (i + 1 < n && !(query[i] == '*' && query[i + 1] == '/')) {
				i++;
			}
			if (i + 1 < n) {
				i += 2;
			} else {
				i = n;
			}
			continue;
		}
		// Single-quoted string: '...''...'
		if (c == '\'') {
			i++;
			while (i < n) {
				if (query[i] == '\'') {
					if (i + 1 < n && query[i + 1] == '\'') {
						i += 2;
						continue;
					}
					i++;
					break;
				}
				i++;
			}
			continue;
		}
		// Double-quoted identifier: "...""..."
		if (c == '"') {
			i++;
			while (i < n) {
				if (query[i] == '"') {
					if (i + 1 < n && query[i + 1] == '"') {
						i += 2;
						continue;
					}
					i++;
					break;
				}
				i++;
			}
			continue;
		}
		// Keyword match at word boundary only
		if (IsIdentChar(c) && (i == 0 || !IsIdentChar(query[i - 1]))) {
			for (auto *kw : VISUALISE_KEYWORDS) {
				size_t kw_len = std::strlen(kw);
				if (MatchesWholeWord(query, i, kw, kw_len)) {
					return true;
				}
			}
			// Skip to end of identifier so we don't rescan every char.
			while (i < n && IsIdentChar(query[i])) {
				i++;
			}
			continue;
		}
		i++;
	}
	return false;
}

static ParserExtensionParseResult ParseFunction(ParserExtensionInfo *, const string &query) {
	if (!ContainsVisualiseKeyword(query)) {
		// Not ours — let DuckDB's parser handle it and surface its own error if any.
		return ParserExtensionParseResult();
	}
	// ggsql's tree-sitter grammar rejects a trailing `;`. Strip trailing whitespace and
	// a single statement terminator before handing the text over.
	string stripped = query;
	while (!stripped.empty() && std::isspace(static_cast<unsigned char>(stripped.back()))) {
		stripped.pop_back();
	}
	if (!stripped.empty() && stripped.back() == ';') {
		stripped.pop_back();
	}
	return ParserExtensionParseResult(make_uniq<GgsqlParseData>(std::move(stripped)));
}

static ParserExtensionPlanResult PlanFunction(ParserExtensionInfo *, ClientContext &context,
                                              duckdb::unique_ptr<ParserExtensionParseData> parse_data) {
	auto &data = static_cast<GgsqlParseData &>(*parse_data);

	ParserExtensionPlanResult result;
	result.function = GgsqlRunTableFunction();
	result.parameters.push_back(Value(data.query));
	result.requires_valid_transaction = false;
	// In silent mode the table function still runs (ExtensionStatement defaults
	// output_type to FORCE_MATERIALIZED, so the server/browser side-effect fires),
	// but marking return_type as NOTHING tells the shell / API clients to skip
	// rendering entirely instead of printing an empty `plot` result.
	result.return_type = IsSilentOutputMode(context) ? StatementReturnType::NOTHING : StatementReturnType::QUERY_RESULT;
	return result;
}

GgsqlParserExtension::GgsqlParserExtension() {
	parse_function = ParseFunction;
	plan_function = PlanFunction;
}

} // namespace duckdb
