#include "ggsql_exec.hpp"
#include "ggsql_bridge.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/main/client_context.hpp"

extern "C" {
#include "ggsql_ext_rs.h"
}

#include <string>

namespace duckdb {

namespace {

// Invokes the Rust entry point and produces either a URL string (success) or throws.
// Releases the returned ByteBuffer in all paths.
string RunGgsqlQuery(ClientContext &context, const string &query) {
	BridgeCtx bctx;
	bctx.outer = &context;
	auto bridge = BuildReaderBridge(bctx);

	ggsql_byte_buffer_t out;
	out.ptr = nullptr;
	out.len = 0;
	out.cap = 0;

	int32_t rc = ggsql_execute(query.data(), query.size(), &bridge, &out);
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
	explicit GgsqlRunBindData(string query_p) : query(std::move(query_p)) {
	}
	string query;
};

struct GgsqlRunGlobalState : public GlobalTableFunctionState {
	bool emitted = false;
};

duckdb::unique_ptr<FunctionData> GgsqlRunBind(ClientContext &, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
	names.emplace_back("plot_url");
	return_types.emplace_back(LogicalType::VARCHAR);
	if (input.inputs.empty() || input.inputs[0].IsNull()) {
		throw InvalidInputException("ggsql_run requires a non-null query argument");
	}
	return make_uniq<GgsqlRunBindData>(input.inputs[0].ToString());
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
	auto url = RunGgsqlQuery(context, bind_data.query);
	output.SetValue(0, 0, Value(url));
	output.SetCardinality(1);
	state.emitted = true;
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
	auto &input = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t q) {
		auto url = RunGgsqlQuery(context, q.GetString());
		return StringVector::AddString(result, url);
	});
}

} // namespace duckdb
