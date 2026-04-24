#include "ggsql_exec.hpp"
#include "ggsql_bridge.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/main/client_context.hpp"

extern "C" {
#include "ggsql_ext_rs.h"
}

#include <string>

namespace duckdb {

namespace {

constexpr int32_t MODE_URL = 0;
constexpr int32_t MODE_SPEC = 1;
constexpr int32_t MODE_HTML = 2;
constexpr int32_t MODE_SILENT = 3;
constexpr const char *SETTING_NAME = "ggsql_output";

// Read the `ggsql_output` setting; parse into the Rust-side mode integer.
// Throws on unrecognised values so a typo surfaces at bind / evaluation time
// rather than silently falling back to the default.
int32_t ResolveOutputMode(ClientContext &context) {
	Value value;
	auto hit = context.TryGetCurrentSetting(SETTING_NAME, value);
	if (!hit) {
		return MODE_SILENT;
	}
	auto mode = StringUtil::Lower(value.ToString());
	if (mode == "silent") {
		return MODE_SILENT;
	}
	if (mode == "url") {
		return MODE_URL;
	}
	if (mode == "spec") {
		return MODE_SPEC;
	}
	if (mode == "html") {
		return MODE_HTML;
	}
	throw InvalidInputException(
	    "ggsql: unrecognised value for ggsql_output '%s' (expected 'silent', 'url', 'spec', or 'html')",
	    mode);
}

// Invokes the Rust entry point; throws on failure, returns the Rust-provided
// UTF-8 payload on success (URL in mode=url, vega-lite JSON in mode=spec).
string RunGgsqlQuery(ClientContext &context, const string &query, int32_t mode) {
	BridgeCtx bctx;
	bctx.outer = &context;
	auto bridge = BuildReaderBridge(bctx);

	ggsql_byte_buffer_t out;
	out.ptr = nullptr;
	out.len = 0;
	out.cap = 0;

	int32_t rc = ggsql_execute(query.data(), query.size(), &bridge, mode, &out);
	string payload;
	if (out.ptr && out.len > 0) {
		payload.assign(reinterpret_cast<const char *>(out.ptr), out.len);
	}
	ggsql_free_buffer(&out);

	if (rc != 0) {
		throw InvalidInputException(payload.empty() ? "ggsql: unknown error" : payload);
	}
	return payload;
}

//===--------------------------------------------------------------------===//
// TableFunction form: one invocation → one row
//===--------------------------------------------------------------------===//

struct GgsqlRunBindData : public TableFunctionData {
	GgsqlRunBindData(string query_p, int32_t mode_p) : query(std::move(query_p)), mode(mode_p) {
	}
	string query;
	int32_t mode;
};

struct GgsqlRunGlobalState : public GlobalTableFunctionState {
	bool emitted = false;
};

duckdb::unique_ptr<FunctionData> GgsqlRunBind(ClientContext &context, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
	// Single stable column name regardless of mode so user SQL like `SELECT plot FROM …`
	// keeps working when the mode is toggled mid-session.
	names.emplace_back("plot");
	return_types.emplace_back(LogicalType::VARCHAR);
	if (input.inputs.empty() || input.inputs[0].IsNull()) {
		throw InvalidInputException("ggsql_run requires a non-null query argument");
	}
	return make_uniq<GgsqlRunBindData>(input.inputs[0].ToString(), ResolveOutputMode(context));
}

duckdb::unique_ptr<GlobalTableFunctionState> GgsqlRunInit(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<GgsqlRunGlobalState>();
}

void GgsqlRunExec(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<GgsqlRunBindData>();
	auto &state = data_p.global_state->Cast<GgsqlRunGlobalState>();
	if (state.emitted) {
		output.SetCardinality(0);
		return;
	}
	auto payload = RunGgsqlQuery(context, bind_data.query, bind_data.mode);
	state.emitted = true;
	if (bind_data.mode == MODE_SILENT) {
		// Side-effect already happened in Rust (server + browser); suppress the row.
		output.SetCardinality(0);
		return;
	}
	output.SetValue(0, 0, Value(payload));
	output.SetCardinality(1);
}

} // namespace

GgsqlRunTableFunction::GgsqlRunTableFunction() {
	name = "ggsql_run";
	arguments.push_back(LogicalType::VARCHAR);
	bind = GgsqlRunBind;
	init_global = GgsqlRunInit;
	function = GgsqlRunExec;
}

//===--------------------------------------------------------------------===//
// Scalar form: SELECT ggsql('…')
//===--------------------------------------------------------------------===//

void GgsqlScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	auto mode = ResolveOutputMode(context);
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t q) {
		auto payload = RunGgsqlQuery(context, q.GetString(), mode);
		return StringVector::AddString(result, payload);
	});
}

} // namespace duckdb
