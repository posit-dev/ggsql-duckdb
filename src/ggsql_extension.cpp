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
