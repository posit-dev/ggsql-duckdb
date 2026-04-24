#pragma once

#include "duckdb.hpp"
#include "duckdb/common/arrow/arrow.hpp"
#include "duckdb/main/connection.hpp"

extern "C" {
#include "ggsql_ext_rs.h"
}

namespace duckdb {

// Per-invocation bridge state. Allocated on the stack in the caller (GgsqlRunExec or
// GgsqlScalarFun), passed as `ctx` through the Rust FFI. Holds the outer ClientContext
// for reference, and a lazily-created inner Connection that persists across every
// exec_sql/register callback within a single ggsql_execute call — so temp tables
// registered by one callback are visible to subsequent ones.
struct BridgeCtx {
	ClientContext *outer = nullptr;
	unique_ptr<Connection> inner;
};

// Build a ggsql_reader_bridge_t whose function pointers dispatch back through `bctx`.
ggsql_reader_bridge_t BuildReaderBridge(BridgeCtx &bctx);

} // namespace duckdb
