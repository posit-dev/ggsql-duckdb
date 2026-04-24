#include "ggsql_bridge.hpp"

#include "duckdb/common/arrow/arrow_wrapper.hpp"
#include "duckdb/common/arrow/result_arrow_wrapper.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/function/table/arrow.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/keyword_helper.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

namespace {

constexpr idx_t ARROW_STREAM_BATCH_SIZE = 100000;

// ---------------------------------------------------------------------------
// Owner block for a stream produced by the inner Connection's Query().
// Kept alive (via the Arrow stream's release callback) until Rust finishes
// consuming the stream, so client_properties.client_context stays valid.
// ---------------------------------------------------------------------------
struct InnerStream {
	unique_ptr<Connection> connection;
	unique_ptr<ResultArrowArrayStreamWrapper> inner;
};

// ---------------------------------------------------------------------------
// Factory for arrow_scan: owns a one-shot ArrowArrayStream handed to us by
// Rust via the register_df callback. Layout-critical: the stream MUST be the
// first field so `reinterpret_cast<ArrowArrayStream *>(factory_ptr)` points
// at a valid stream for DuckDB's stream_factory_get_schema contract.
// ---------------------------------------------------------------------------
struct OwnedArrowStreamFactory {
	ArrowArrayStream stream {};
	bool consumed = false;

	~OwnedArrowStreamFactory() {
		if (!consumed && stream.release) {
			stream.release(&stream);
		}
	}
};

void WriteError(ggsql_byte_buffer_t *out_err, const std::string &msg) {
	if (!out_err) {
		return;
	}
	out_err->ptr = nullptr;
	out_err->len = 0;
	out_err->cap = 0;
	if (msg.empty()) {
		return;
	}
	auto *buf = static_cast<uint8_t *>(std::malloc(msg.size()));
	if (!buf) {
		return;
	}
	std::memcpy(buf, msg.data(), msg.size());
	out_err->ptr = buf;
	out_err->len = msg.size();
	out_err->cap = msg.size();
}

extern "C" void CppFreeBuffer(ggsql_byte_buffer_t *buf) {
	if (!buf || !buf->ptr) {
		return;
	}
	std::free(buf->ptr);
	buf->ptr = nullptr;
	buf->len = 0;
	buf->cap = 0;
}

// --- Arrow stream forwarders (exec_sql path) -------------------------------

extern "C" int InnerGetSchema(ArrowArrayStream *stream, ArrowSchema *out) {
	auto *self = static_cast<InnerStream *>(stream->private_data);
	return self->inner->stream.get_schema(&self->inner->stream, out);
}

extern "C" int InnerGetNext(ArrowArrayStream *stream, ArrowArray *out) {
	auto *self = static_cast<InnerStream *>(stream->private_data);
	return self->inner->stream.get_next(&self->inner->stream, out);
}

extern "C" const char *InnerGetLastError(ArrowArrayStream *stream) {
	auto *self = static_cast<InnerStream *>(stream->private_data);
	return self->inner->stream.get_last_error(&self->inner->stream);
}

extern "C" void InnerRelease(ArrowArrayStream *stream) {
	if (!stream || !stream->release) {
		return;
	}
	stream->release = nullptr;
	auto *self = static_cast<InnerStream *>(stream->private_data);
	stream->private_data = nullptr;
	delete self;
}

// --- arrow_scan factory callbacks (register_df path) -----------------------

unique_ptr<ArrowArrayStreamWrapper> FactoryProduce(uintptr_t ptr, ArrowStreamParameters &) {
	auto *factory = reinterpret_cast<OwnedArrowStreamFactory *>(ptr);
	if (factory->consumed) {
		throw InvalidInputException("arrow stream already consumed");
	}
	auto wrapper = make_uniq<ArrowArrayStreamWrapper>();
	wrapper->arrow_array_stream = factory->stream; // transfer ownership of callbacks + private_data
	std::memset(&factory->stream, 0, sizeof(factory->stream));
	factory->consumed = true;
	return wrapper;
}

void FactoryGetSchema(ArrowArrayStream *factory_as_stream, ArrowSchema &out) {
	// The "stream" pointer here is actually our factory (which has ArrowArrayStream as
	// its first field — see the comment on OwnedArrowStreamFactory). Cast back and
	// forward to the embedded stream's get_schema.
	auto *factory = reinterpret_cast<OwnedArrowStreamFactory *>(factory_as_stream);
	if (factory->consumed) {
		throw InvalidInputException("arrow stream already consumed");
	}
	if (!factory->stream.get_schema) {
		throw InternalException("arrow stream has no get_schema callback");
	}
	int rc = factory->stream.get_schema(&factory->stream, &out);
	if (rc != 0) {
		const char *msg = factory->stream.get_last_error ? factory->stream.get_last_error(&factory->stream) : nullptr;
		throw InvalidInputException("arrow get_schema failed: %s", msg ? msg : "unknown error");
	}
}

void EnsureInnerConnection(BridgeCtx &bctx) {
	if (!bctx.inner) {
		bctx.inner = make_uniq<Connection>(*bctx.outer->db);
	}
}

// --- Bridge callbacks ------------------------------------------------------

extern "C" int32_t ExecSqlCallback(void *ctx, const char *sql, size_t sql_len,
                                   struct ArrowArrayStream *out_stream, ggsql_byte_buffer_t *out_err) {
	if (out_stream) {
		std::memset(out_stream, 0, sizeof(*out_stream));
	}
	if (out_err) {
		out_err->ptr = nullptr;
		out_err->len = 0;
		out_err->cap = 0;
	}

	if (!ctx || !sql || !out_stream) {
		WriteError(out_err, "ggsql bridge: null pointer in exec_sql callback");
		return 1;
	}

	auto *bctx = static_cast<BridgeCtx *>(ctx);
	std::string query(sql, sql_len);

	auto holder = make_uniq<InnerStream>();
	unique_ptr<QueryResult> result;
	try {
		EnsureInnerConnection(*bctx);
		result = bctx->inner->Query(query);
	} catch (const std::exception &ex) {
		WriteError(out_err, ex.what());
		return 1;
	} catch (...) {
		WriteError(out_err, "ggsql bridge: unknown exception running SQL");
		return 1;
	}

	if (!result) {
		WriteError(out_err, "ggsql bridge: Query returned null");
		return 1;
	}
	if (result->HasError()) {
		WriteError(out_err, result->GetError());
		return 1;
	}

	// The inner Connection must outlive the stream (QueryResult holds a raw pointer to
	// its ClientContext). Own both inside `holder` and hand off via the stream's
	// release callback. We use the inner `bctx->inner` directly — it already lives for
	// the full ggsql_execute call.
	try {
		holder->inner = make_uniq<ResultArrowArrayStreamWrapper>(std::move(result), ARROW_STREAM_BATCH_SIZE);
	} catch (const std::exception &ex) {
		WriteError(out_err, ex.what());
		return 1;
	} catch (...) {
		WriteError(out_err, "ggsql bridge: unknown exception building arrow stream");
		return 1;
	}

	out_stream->get_schema = InnerGetSchema;
	out_stream->get_next = InnerGetNext;
	out_stream->get_last_error = InnerGetLastError;
	out_stream->release = InnerRelease;
	out_stream->private_data = holder.release();
	return 0;
}

extern "C" int32_t RegisterDfCallback(void *ctx, const char *name_p, size_t name_len,
                                      struct ArrowArrayStream *stream, int replace,
                                      ggsql_byte_buffer_t *out_err) {
	if (out_err) {
		out_err->ptr = nullptr;
		out_err->len = 0;
		out_err->cap = 0;
	}
	if (!ctx || !name_p || !stream) {
		WriteError(out_err, "ggsql bridge: null pointer in register_df callback");
		return 1;
	}

	auto *bctx = static_cast<BridgeCtx *>(ctx);
	std::string name(name_p, name_len);

	try {
		EnsureInnerConnection(*bctx);

		// Take ownership of the stream from Rust.
		auto factory = make_uniq<OwnedArrowStreamFactory>();
		factory->stream = *stream;
		std::memset(stream, 0, sizeof(*stream));

		// Materialise as a TEMP TABLE — this consumes the stream eagerly so the factory
		// (and therefore the stream) can be safely destroyed when we return.
		// TODO(ggsql-register-regression): this is ggsql 0.2.7 engine plumbing. When
		// ggsql stops materialising the main SELECT back into the reader, delete this
		// whole callback + OwnedArrowStreamFactory plumbing.
		std::string ddl = std::string(replace ? "CREATE OR REPLACE" : "CREATE")
		    + " TEMP TABLE " + KeywordHelper::WriteQuoted(name, '"')
		    + " AS SELECT * FROM arrow_scan(?, ?, ?)";

		auto produce = reinterpret_cast<uintptr_t>(&FactoryProduce);
		auto get_schema = reinterpret_cast<uintptr_t>(&FactoryGetSchema);
		auto factory_ptr = reinterpret_cast<uintptr_t>(factory.get());

		auto result = bctx->inner->Query(
		    ddl, Value::POINTER(factory_ptr), Value::POINTER(produce), Value::POINTER(get_schema));
		if (!result) {
			WriteError(out_err, "ggsql bridge: register Query returned null");
			return 1;
		}
		if (result->HasError()) {
			WriteError(out_err, result->GetError());
			return 1;
		}
	} catch (const std::exception &ex) {
		WriteError(out_err, ex.what());
		return 1;
	} catch (...) {
		WriteError(out_err, "ggsql bridge: unknown exception in register_df");
		return 1;
	}
	return 0;
}

} // namespace

ggsql_reader_bridge_t BuildReaderBridge(BridgeCtx &bctx) {
	ggsql_reader_bridge_t bridge;
	bridge.ctx = static_cast<void *>(&bctx);
	bridge.exec_sql = ExecSqlCallback;
	bridge.register_df = RegisterDfCallback;
	bridge.free_buffer = CppFreeBuffer;
	return bridge;
}

} // namespace duckdb
