#define DUCKDB_EXTENSION_MAIN

#include "ggsql_extension.hpp"
#include "ggsql_exec.hpp"
#include "ggsql_parser.hpp"

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parser_extension.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	// Public scalar form: SELECT ggsql('<ggsql query>')
	CreateScalarFunctionInfo ggsql_info(
	    ScalarFunction("ggsql", {LogicalType::VARCHAR}, LogicalType::VARCHAR, GgsqlScalarFun));
	// The bare RegisterFunction overload sets ALTER_ON_CONFLICT internally; match it.
	ggsql_info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	FunctionDescription ggsql_desc;
	ggsql_desc.parameter_names = {"query"};
	ggsql_desc.description =
	    "Executes a ggsql query (SQL plus VISUALISE/DRAW clauses) and returns the plot in the format selected by "
	    "the ggsql_output setting (browser URL, vega-lite spec, HTML, or SVG).";
	ggsql_desc.examples = {"ggsql('SELECT range AS x, range*range AS y FROM range(10) VISUALISE x, y DRAW line')"};
	ggsql_desc.categories = {"plotting"};
	ggsql_info.descriptions.push_back(std::move(ggsql_desc));
	loader.RegisterFunction(std::move(ggsql_info));

	// Save form: SELECT ggsql_save('<query>', 'plot.svg') — the writer is
	// inferred from the file extension and the payload written to disk.
	CreateScalarFunctionInfo ggsql_save_info(
	    ScalarFunction("ggsql_save", {LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, GgsqlSaveFun));
	ggsql_save_info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	FunctionDescription ggsql_save_desc;
	ggsql_save_desc.parameter_names = {"query", "path"};
	ggsql_save_desc.description =
	    "Renders a ggsql query straight to a file; the writer is inferred from the file extension "
	    "(.svg, .pdf, .hep, .html, .json) and the output path is returned.";
	ggsql_save_desc.examples = {"ggsql_save('SELECT range AS x FROM range(10) VISUALISE x DRAW line', 'plot.svg')"};
	ggsql_save_desc.categories = {"plotting"};
	ggsql_save_info.descriptions.push_back(std::move(ggsql_save_desc));
	loader.RegisterFunction(std::move(ggsql_save_info));

	// Table-function form, also used by the parser extension's plan. Registered
	// standalone so binary output modes (pdf/hep) are reachable with their
	// proper BLOB return type.
	CreateTableFunctionInfo ggsql_run_info {GgsqlRunTableFunction()};
	ggsql_run_info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	FunctionDescription ggsql_run_desc;
	ggsql_run_desc.parameter_names = {"query"};
	ggsql_run_desc.description =
	    "Executes a ggsql query and returns the plot as a one-row table; used for binary output modes "
	    "('pdf', 'hep') where the result column is typed as BLOB.";
	ggsql_run_desc.examples = {"SELECT plot FROM ggsql_run('SELECT range AS x FROM range(10) VISUALISE x DRAW line')"};
	ggsql_run_desc.categories = {"plotting"};
	ggsql_run_info.descriptions.push_back(std::move(ggsql_run_desc));
	loader.RegisterFunction(std::move(ggsql_run_info));

	// Primary surface: intercept any statement containing VISUALISE/VISUALIZE at the
	// top level and route it through the same Rust entry point.
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	ParserExtension::Register(config, GgsqlParserExtension());

	// Per-session output mode. Recognised values:
	//   'silent' (default) — open the browser, emit no visible rows
	//   'url'              — open the browser, return the plot URL
	//   'spec'             — return the raw vega-lite JSON; no browser, no HTTP server
	//   'html'             — return a self-contained HTML document; no browser, no HTTP server
	//   'svg'              — return the plot as SVG text (native renderer)
	//   'pdf'              — return the plot as PDF bytes (BLOB from ggsql_run)
	//   'hep'              — return the plot as a .hep plot document (BLOB from ggsql_run)
	config.AddExtensionOption("ggsql_output",
	                          "Output mode for ggsql queries. 'silent' (default) opens the browser and emits "
	                          "no rows; 'url' opens the browser and returns the plot URL; 'spec' returns the "
	                          "vega-lite JSON; 'html' returns a self-contained HTML document; 'svg' returns "
	                          "SVG text; 'pdf' and 'hep' return binary payloads (BLOB from ggsql_run).",
	                          LogicalType::VARCHAR, Value("silent"));

	// Options for the native writers ('svg'/'pdf'/'hep') and the browser/html
	// display (which renders through the same hephaestus pipeline), forwarded
	// verbatim to ggsql's WriterOptions — e.g. 'width=800;height=600;units=px'.
	// Shared keys: width, height, units, dpi, background; each writer adds its
	// own (svg: text, embed-fonts, id-prefix; pdf: compress, links; hep: lossy,
	// embed-fonts). Unknown keys are rejected with an error naming them. Only
	// 'spec' (raw vega-lite JSON) rejects options outright.
	config.AddExtensionOption("ggsql_writer_options",
	                          "Options for ggsql's writers, as semicolon-separated key=value pairs, e.g. "
	                          "'width=800;height=600'. Applies to the 'svg', 'pdf', 'hep', 'html' modes and the "
	                          "browser display; must be empty for 'spec'. Shared keys: width, height, units, "
	                          "dpi, background.",
	                          LogicalType::VARCHAR, Value(""));
}

void GgsqlExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string GgsqlExtension::Name() {
	return "ggsql";
}

std::string GgsqlExtension::Version() const {
#ifdef EXT_VERSION_GGSQL
	return EXT_VERSION_GGSQL;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(ggsql, loader) {
	duckdb::LoadInternal(loader);
}
}
