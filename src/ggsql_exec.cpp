#include "ggsql_exec.hpp"
#include "ggsql_bridge.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/client_context.hpp"

extern "C" {
#include "ggsql_ext_rs.h"
}

#include <fstream>
#include <string>

namespace duckdb {

namespace {

constexpr const char *OUTPUT_SETTING = "ggsql_output";
constexpr const char *OPTIONS_SETTING = "ggsql_writer_options";

// The resolved output configuration for one query: which writer to run and the
// raw options string to pass through to it. `binary` marks writers whose
// payload is not UTF-8 text — those surface as BLOB from ggsql_run.
struct OutputConfig {
	string writer;
	string options;
	bool binary;
};

// Read the `ggsql_output` and `ggsql_writer_options` settings. Throws on an
// unrecognised output value so a typo surfaces at bind / evaluation time rather
// than silently falling back to the default. The options string is forwarded
// verbatim — ggsql's WriterOptions does the parsing and rejects unknown keys
// with a message naming them.
OutputConfig ResolveOutputConfig(ClientContext &context) {
	Value value;
	context.TryGetCurrentSetting(OUTPUT_SETTING, value);
	auto mode = StringUtil::Lower(value.ToString());

	OutputConfig cfg;
	cfg.writer = mode;
	cfg.binary = mode == "pdf" || mode == "hep";
	if (mode != "silent" && mode != "url" && mode != "spec" && mode != "html" && !cfg.binary && mode != "svg") {
		throw InvalidInputException("ggsql: unrecognised value for ggsql_output '%s' (expected 'silent', 'url', "
		                            "'spec', 'html', 'svg', 'pdf', or 'hep')",
		                            mode);
	}

	Value options;
	context.TryGetCurrentSetting(OPTIONS_SETTING, options);
	cfg.options = options.IsNull() ? "" : options.ToString();
	return cfg;
}

// Invokes the Rust entry point; throws on failure, returns the Rust-provided
// payload on success (text for url/spec/html/svg, raw bytes for pdf/hep, empty
// for silent).
string RunGgsqlQuery(ClientContext &context, const string &query, const OutputConfig &cfg) {
	if (context.IsInterrupted()) {
		throw InterruptException();
	}
	BridgeCtx bctx(context);
	auto bridge = BuildReaderBridge(bctx);

	ggsql_byte_buffer_t out;
	out.ptr = nullptr;
	out.len = 0;
	out.cap = 0;

	int32_t rc = ggsql_execute(query.data(), query.size(), &bridge, cfg.writer.data(), cfg.writer.size(),
	                           cfg.options.data(), cfg.options.size(), &out);
	string payload;
	if (out.ptr && out.len > 0) {
		payload.assign(reinterpret_cast<const char *>(out.ptr), out.len);
	}
	ggsql_free_buffer(&out);

	// The Rust/Arrow bridge transports errors as strings. Restore DuckDB's
	// interruption type after releasing the FFI buffer, rather than reporting it
	// as invalid input (or returning a result after cancellation).
	if (context.IsInterrupted()) {
		throw InterruptException();
	}
	if (rc != 0) {
		throw InvalidInputException(payload.empty() ? "ggsql: unknown error" : payload);
	}
	return payload;
}

//===--------------------------------------------------------------------===//
// TableFunction form: one invocation → one row
//===--------------------------------------------------------------------===//

struct GgsqlRunBindData : public TableFunctionData {
	GgsqlRunBindData(string query_p, OutputConfig cfg_p) : query(std::move(query_p)), cfg(std::move(cfg_p)) {
	}
	string query;
	OutputConfig cfg;
};

struct GgsqlRunGlobalState : public GlobalTableFunctionState {
	bool emitted = false;
};

duckdb::unique_ptr<FunctionData> GgsqlRunBind(ClientContext &context, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
	// Single stable column name regardless of mode so user SQL like `SELECT plot FROM …`
	// keeps working when the mode is toggled mid-session.
	names.emplace_back("plot");
	auto cfg = ResolveOutputConfig(context);
	// Binary writers (pdf, hep) surface as BLOB; everything else is UTF-8 text.
	return_types.emplace_back(cfg.binary ? LogicalType::BLOB : LogicalType::VARCHAR);
	if (input.inputs.empty() || input.inputs[0].IsNull()) {
		throw InvalidInputException("ggsql_run requires a non-null query argument");
	}
	return make_uniq<GgsqlRunBindData>(input.inputs[0].ToString(), std::move(cfg));
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
	auto payload = RunGgsqlQuery(context, bind_data.query, bind_data.cfg);
	state.emitted = true;
	if (bind_data.cfg.writer == "silent") {
		// Side-effect already happened in Rust (server + browser); suppress the row.
		output.SetCardinality(0);
		return;
	}
	// Note: Value::BLOB(const string&) parses its argument as a blob *literal*
	// (expecting \xAA escapes); the pointer/length overload takes raw bytes.
	output.SetValue(
	    0, 0, bind_data.cfg.binary ? Value::BLOB(const_data_ptr_cast(payload.data()), payload.size()) : Value(payload));
	output.SetCardinality(1);
}

} // namespace

bool IsSilentOutputMode(ClientContext &context) {
	return ResolveOutputConfig(context).writer == "silent";
}

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
	auto cfg = ResolveOutputConfig(context);
	auto &input = args.data[0];
	// The scalar's return type is fixed at registration (VARCHAR), so binary
	// payloads (pdf/hep) come back as raw bytes in a VARCHAR here; ggsql_run
	// gives them their proper BLOB type.
	UnaryExecutor::Execute<string_t, string_t>(input, result, args.size(), [&](string_t q) {
		auto payload = RunGgsqlQuery(context, q.GetString(), cfg);
		return StringVector::AddString(result, payload);
	});
}

//===--------------------------------------------------------------------===//
// Save form: SELECT ggsql_save('…', 'path.svg')
//===--------------------------------------------------------------------===//

// Map a file extension to a writer name. svg/pdf/hep are the native writers;
// html and json (vega-lite spec) reuse the browser-free legacy paths. Anything
// else is rejected here so a typo like 'plot.sv' fails before any rendering
// work happens.
static string WriterForPath(const string &path) {
	auto dot = path.find_last_of('.');
	if (dot == string::npos || dot == path.size() - 1) {
		throw InvalidInputException("ggsql: cannot infer an output format from '%s' — the path has no extension "
		                            "(supported: .svg, .pdf, .hep, .html, .json)",
		                            path);
	}
	auto ext = StringUtil::Lower(path.substr(dot + 1));
	if (ext == "svg" || ext == "pdf" || ext == "hep" || ext == "html") {
		return ext;
	}
	if (ext == "json") {
		return "spec";
	}
	throw InvalidInputException("ggsql: unsupported output format '.%s' in '%s' (supported: .svg, .pdf, .hep, "
	                            ".html, .json)",
	                            ext, path);
}

void GgsqlSaveFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	// Writer options still come from the session setting; only the writer
	// itself is taken from the path, so `SET ggsql_writer_options = 'width=…'`
	// affects saved output exactly as it affects returned output.
	auto session_cfg = ResolveOutputConfig(context);

	BinaryExecutor::Execute<string_t, string_t, string_t>(
	    args.data[0], args.data[1], result, args.size(), [&](string_t q, string_t p) {
		    auto path = p.GetString();
		    OutputConfig cfg;
		    cfg.writer = WriterForPath(path);
		    cfg.binary = cfg.writer == "pdf" || cfg.writer == "hep";
		    cfg.options = session_cfg.options;

		    auto payload = RunGgsqlQuery(context, q.GetString(), cfg);

		    std::ofstream file(path, std::ios::binary | std::ios::trunc);
		    if (!file) {
			    throw IOException("ggsql: cannot open '%s' for writing", path);
		    }
		    file.write(payload.data(), static_cast<std::streamsize>(payload.size()));
		    file.close();
		    if (!file) {
			    throw IOException("ggsql: failed while writing '%s'", path);
		    }
		    return StringVector::AddString(result, path);
	    });
}

} // namespace duckdb
