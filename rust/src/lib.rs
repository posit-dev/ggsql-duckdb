//! ggsql_ext_rs — Rust side of the ggsql DuckDB extension.
//!
//! Exposes a C ABI consumed by the C++ extension code. See
//! `rust/include/ggsql_ext_rs.h` for the matching C declarations.

mod dialect;
mod ffi;
mod reader;
mod server;

use std::os::raw::{c_char, c_int};
use std::panic::{catch_unwind, AssertUnwindSafe};

use ggsql::reader::Reader;
use ggsql::writer::{VegaLiteWriter, Writer};

pub use crate::ffi::{ByteBuffer, ReaderBridge};
use crate::reader::CallbackReader;

/// Execute a ggsql query end-to-end: parse → SQL-via-bridge → vega-lite → serve → open browser.
///
/// On success, `out` holds a UTF-8 URL the caller can return to the user. On failure, `out`
/// holds a UTF-8 error message.
///
/// # Safety
///
/// - `query` must point to at least `query_len` valid bytes (not required to be NUL-terminated).
/// - `bridge` must point to an initialised `ReaderBridge`. Its `ctx` pointer and function
///   pointers must stay valid for the duration of this call.
/// - `out` must point to a writable `ByteBuffer`. Whatever it contained on entry is ignored.
///   On return, ownership of any allocated bytes transfers to the caller, which must release
///   them via `ggsql_free_buffer`.
#[no_mangle]
pub unsafe extern "C" fn ggsql_execute(
    query: *const c_char,
    query_len: usize,
    bridge: *const ReaderBridge,
    out: *mut ByteBuffer,
) -> c_int {
    if query.is_null() || bridge.is_null() || out.is_null() {
        return 1;
    }
    *out = ByteBuffer::EMPTY;

    let query_bytes = std::slice::from_raw_parts(query as *const u8, query_len);
    let bridge_copy = ReaderBridge {
        ctx: (*bridge).ctx,
        exec_sql: (*bridge).exec_sql,
        register_df: (*bridge).register_df,
        free_buffer: (*bridge).free_buffer,
    };

    let result = catch_unwind(AssertUnwindSafe(|| run(query_bytes, bridge_copy)));

    match result {
        Ok(Ok(url)) => {
            *out = ByteBuffer::from_vec(url.into_bytes());
            0
        }
        Ok(Err(msg)) => {
            *out = ByteBuffer::from_vec(msg.into_bytes());
            1
        }
        Err(_) => {
            *out = ByteBuffer::from_vec(b"ggsql: panic in Rust side".to_vec());
            2
        }
    }
}

/// Free a `ByteBuffer` populated by Rust. Safe to call with an all-zero buffer.
///
/// # Safety
///
/// `buf` must be non-null and must point to a `ByteBuffer` whose `ptr`/`len`/`cap` either
/// describe a prior `ByteBuffer::from_vec` allocation or are all zero.
#[no_mangle]
pub unsafe extern "C" fn ggsql_free_buffer(buf: *mut ByteBuffer) {
    if buf.is_null() {
        return;
    }
    let b = &mut *buf;
    if !b.ptr.is_null() && b.cap > 0 {
        drop(Vec::from_raw_parts(b.ptr, b.len, b.cap));
    }
    *b = ByteBuffer::EMPTY;
}

fn run(query_bytes: &[u8], bridge: ReaderBridge) -> Result<String, String> {
    let query = std::str::from_utf8(query_bytes)
        .map_err(|e| format!("ggsql: query is not valid UTF-8: {}", e))?;

    let reader = CallbackReader::new(bridge);

    let spec = reader.execute(query).map_err(|e| format!("ggsql: {}", e))?;

    let json = VegaLiteWriter::new()
        .render(&spec)
        .map_err(|e| format!("ggsql: render failed: {}", e))?;

    let registered =
        server::register_spec(json).map_err(|e| format!("ggsql: serve failed: {}", e))?;

    // Best-effort — if the browser fails to open we still return the plot URL so the
    // user can open it manually. Skip entirely when GGSQL_NO_OPEN_BROWSER is set so
    // SQL tests and CI don't spawn browser tabs.
    //
    // We hand `open::that` the stable *root* URL rather than the per-plot path so
    // repeated invocations reuse the existing browser tab (the OS `open` refocuses
    // when the URL matches an already-open tab). Inside the tab, the SPA polls for
    // /api/latest and navigates to the new plot via history.pushState.
    if std::env::var_os("GGSQL_NO_OPEN_BROWSER").is_none() {
        let _ = open::that(&registered.open_url);
    }

    Ok(registered.plot_url)
}
