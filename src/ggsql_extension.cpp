#define DUCKDB_EXTENSION_MAIN

#include "ggsql_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

namespace duckdb {

inline void GgsqlScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "Ggsql " + name.GetString() + " 🐥");
	});
}

inline void GgsqlOpenSSLVersionScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &name_vector = args.data[0];
	UnaryExecutor::Execute<string_t, string_t>(name_vector, result, args.size(), [&](string_t name) {
		return StringVector::AddString(result, "Ggsql " + name.GetString() + ", my linked OpenSSL version is " +
		                                           OPENSSL_VERSION_TEXT);
	});
}

static void LoadInternal(ExtensionLoader &loader) {
	// Register a scalar function
	auto ggsql_scalar_function = ScalarFunction("ggsql", {LogicalType::VARCHAR}, LogicalType::VARCHAR, GgsqlScalarFun);
	loader.RegisterFunction(ggsql_scalar_function);

	// Register another scalar function
	auto ggsql_openssl_version_scalar_function = ScalarFunction("ggsql_openssl_version", {LogicalType::VARCHAR},
	                                                            LogicalType::VARCHAR, GgsqlOpenSSLVersionScalarFun);
	loader.RegisterFunction(ggsql_openssl_version_scalar_function);
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
