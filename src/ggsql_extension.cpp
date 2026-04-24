#define DUCKDB_EXTENSION_MAIN

#include "ggsql_extension.hpp"
#include "ggsql_exec.hpp"
#include "ggsql_parser.hpp"

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/parser/parser_extension.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
	// Public scalar form: SELECT ggsql('<ggsql query>')
	ScalarFunction ggsql_scalar("ggsql", {LogicalType::VARCHAR}, LogicalType::VARCHAR, GgsqlScalarFun);
	loader.RegisterFunction(ggsql_scalar);

	// Primary surface: intercept any statement containing VISUALISE/VISUALIZE at the
	// top level and route it through the same Rust entry point.
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	ParserExtension::Register(config, GgsqlParserExtension());

	// Per-session output mode. Recognised values:
	//   'silent' (default) — open the browser, emit no visible rows
	//   'url'              — open the browser, return the plot URL
	//   'spec'             — return the raw vega-lite JSON; no browser, no HTTP server
	//   'html'             — return a self-contained HTML document; no browser, no HTTP server
	config.AddExtensionOption("ggsql_output",
	                          "Output mode for ggsql queries. 'silent' (default) opens the browser and emits "
	                          "no rows; 'url' opens the browser and returns the plot URL; 'spec' returns the "
	                          "vega-lite JSON; 'html' returns a self-contained HTML document.",
	                          LogicalType::VARCHAR, Value("silent"));
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
