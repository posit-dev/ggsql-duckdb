#include "ggsql_bridge.hpp"

#include "duckdb/common/arrow/result_arrow_wrapper.hpp"
#include "duckdb/main/client_context.hpp"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace duckdb {

// Query() blocks during binding and execution, so checking only between bridge
// calls cannot cancel a long-running inner query. Interrupt() and IsInterrupted()
// use DuckDB's atomic flag and do not acquire either connection's context lock.
class InterruptForwarder {
public:
	InterruptForwarder(ClientContext &outer, Connection &inner)
	    : outer(outer), inner(inner), worker([this] { Run(); }) {
	}

	~InterruptForwarder() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopped = true;
		}
		condition.notify_one();
		worker.join();
	}

private:
	void Run() {
		std::unique_lock<std::mutex> lock(mutex);
		while (!stopped) {
			if (outer.IsInterrupted()) {
				inner.Interrupt();
			}
			// Keep forwarding: starting another inner query clears its interrupt flag.
			condition.wait_for(lock, std::chrono::milliseconds(10), [this] { return stopped; });
		}
	}

	ClientContext &outer;
	Connection &inner;
	std::mutex mutex;
	std::condition_variable condition;
	bool stopped = false;
	std::thread worker;
};

BridgeCtx::BridgeCtx(ClientContext &outer) : outer(outer) {
}

BridgeCtx::~BridgeCtx() = default;

bool BridgeCtx::IsInterrupted() const {
	return outer.IsInterrupted();
}

Connection &BridgeCtx::GetInnerConnection() {
	if (IsInterrupted()) {
		throw InterruptException();
	}
	if (!inner) {
		inner = make_uniq<Connection>(*outer.db);
		interrupt_forwarder = make_uniq<InterruptForwarder>(outer, *inner);
	}
	return *inner;
}

namespace {

constexpr idx_t ARROW_STREAM_BATCH_SIZE = 100000;

// ---------------------------------------------------------------------------
// Own the result until Rust releases the stream. The bridge and its connection
// outlive all streams consumed during ggsql_execute.
// ---------------------------------------------------------------------------
struct InnerStream {
	explicit InnerStream(BridgeCtx &bridge) : bridge(bridge) {
	}

	BridgeCtx &bridge;
	bool interrupted = false;
	unique_ptr<ResultArrowArrayStreamWrapper> inner;
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
	if (self->bridge.IsInterrupted()) {
		self->interrupted = true;
		out->release = nullptr;
		return EINTR;
	}
	return self->inner->stream.get_schema(&self->inner->stream, out);
}

extern "C" int InnerGetNext(ArrowArrayStream *stream, ArrowArray *out) {
	auto *self = static_cast<InnerStream *>(stream->private_data);
	if (self->bridge.IsInterrupted()) {
		self->interrupted = true;
		out->release = nullptr;
		return EINTR;
	}
	return self->inner->stream.get_next(&self->inner->stream, out);
}

extern "C" const char *InnerGetLastError(ArrowArrayStream *stream) {
	auto *self = static_cast<InnerStream *>(stream->private_data);
	if (self->interrupted) {
		return "Interrupted!";
	}
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

// --- Bridge callbacks ------------------------------------------------------

extern "C" int32_t ExecSqlCallback(void *ctx, const char *sql, size_t sql_len, struct ArrowArrayStream *out_stream,
                                   ggsql_byte_buffer_t *out_err) {
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

	auto holder = make_uniq<InnerStream>(*bctx);
	unique_ptr<QueryResult> result;
	try {
		result = bctx->GetInnerConnection().Query(query);
		if (bctx->IsInterrupted()) {
			throw InterruptException();
		}
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

	// QueryResult holds a raw ClientContext pointer. BridgeCtx keeps the inner
	// connection alive until Rust has consumed and released every stream.
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

} // namespace

ggsql_reader_bridge_t BuildReaderBridge(BridgeCtx &bctx) {
	ggsql_reader_bridge_t bridge;
	bridge.ctx = static_cast<void *>(&bctx);
	bridge.exec_sql = ExecSqlCallback;
	bridge.free_buffer = CppFreeBuffer;
	return bridge;
}

} // namespace duckdb
