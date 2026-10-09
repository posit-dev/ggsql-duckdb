#pragma once

#include "duckdb.hpp"
#include "duckdb/common/arrow/arrow.hpp"
#include "duckdb/main/connection.hpp"

extern "C" {
#include "ggsql_ext_rs.h"
}

namespace duckdb {

class InterruptForwarder;

// Per-invocation bridge state. Allocated on the stack in the caller (GgsqlRunExec or
// GgsqlScalarFun), passed as `ctx` through the Rust FFI. Holds the outer ClientContext
// for reference, and a lazily-created inner Connection that persists across every
// exec_sql callback within a single ggsql_execute call — so temp tables created by one
// callback (e.g. ggsql's CTE materialisation) are visible to subsequent ones.
struct BridgeCtx {
	explicit BridgeCtx(ClientContext &outer);
	~BridgeCtx();

	bool IsInterrupted() const;
	Connection &GetInnerConnection();

private:
	ClientContext &outer;
	unique_ptr<Connection> inner;
	// Destroy the forwarder (and join its thread) before destroying the connection.
	unique_ptr<InterruptForwarder> interrupt_forwarder;
};

// Build a ggsql_reader_bridge_t whose function pointers dispatch back through `bctx`.
ggsql_reader_bridge_t BuildReaderBridge(BridgeCtx &bctx);

} // namespace duckdb
