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
use ggsql::writer::{HepWriter, PdfWriter, SvgWriter, VegaLiteWriter, Writer, WriterOptions};

pub use crate::ffi::{ByteBuffer, ReaderBridge};
use crate::reader::CallbackReader;

/// Execute a ggsql query end-to-end: parse → SQL-via-bridge → render with the
/// requested writer.
///
/// `writer` names the output path: the browser-backed pseudo-writers `silent`,
/// `url`, and `html`, the raw `spec` (vega-lite JSON), or one of the native
/// writers `svg`, `pdf`, `hep`. `options` is a `key=value;key=value` string
/// parsed by ggsql's `WriterOptions` and handed to the native writer's
/// `from_options`; it must be empty for the browser/spec paths.
///
/// On success, `out` holds the writer's payload (text for svg/spec/html/url,
/// raw bytes for pdf/hep, empty for silent). On failure, `out` holds a UTF-8
/// error message.
///
/// # Safety
///
/// - `query`/`writer`/`options` must point to at least `*_len` valid UTF-8
///   bytes (not required to be NUL-terminated).
/// - `bridge` must point to an initialised `ReaderBridge`. Its `ctx` pointer and
///   function pointers must stay valid for the duration of this call.
/// - `out` must point to a writable `ByteBuffer`. Whatever it contained on entry
///   is ignored. On return, ownership of any allocated bytes transfers to the
///   caller, which must release them via `ggsql_free_buffer`.
#[no_mangle]
pub unsafe extern "C" fn ggsql_execute(
    query: *const c_char,
    query_len: usize,
    bridge: *const ReaderBridge,
    writer: *const c_char,
    writer_len: usize,
    options: *const c_char,
    options_len: usize,
    out: *mut ByteBuffer,
) -> c_int {
    if query.is_null() || bridge.is_null() || writer.is_null() || options.is_null() || out.is_null() {
        return 1;
    }
    *out = ByteBuffer::EMPTY;

    let query_bytes = std::slice::from_raw_parts(query as *const u8, query_len);
    let writer_bytes = std::slice::from_raw_parts(writer as *const u8, writer_len);
    let options_bytes = std::slice::from_raw_parts(options as *const u8, options_len);
    let bridge_copy = ReaderBridge {
        ctx: (*bridge).ctx,
        exec_sql: (*bridge).exec_sql,
        free_buffer: (*bridge).free_buffer,
    };

    let result = catch_unwind(AssertUnwindSafe(|| {
        run(query_bytes, bridge_copy, writer_bytes, options_bytes)
    }));

    match result {
        Ok(Ok(payload)) => {
            *out = ByteBuffer::from_vec(payload);
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

fn run(
    query_bytes: &[u8],
    bridge: ReaderBridge,
    writer_bytes: &[u8],
    options_bytes: &[u8],
) -> Result<Vec<u8>, String> {
    let query = std::str::from_utf8(query_bytes)
        .map_err(|e| format!("ggsql: query is not valid UTF-8: {}", e))?;
    let writer = std::str::from_utf8(writer_bytes)
        .map_err(|e| format!("ggsql: writer name is not valid UTF-8: {}", e))?;
    let options = std::str::from_utf8(options_bytes)
        .map_err(|e| format!("ggsql: writer options are not valid UTF-8: {}", e))?;

    let reader = CallbackReader::new(bridge);

    let spec = reader.execute(query).map_err(|e| format!("ggsql: {}", e))?;

    match writer {
        "svg" => render_with::<SvgWriter>(&spec, options),
        "pdf" => render_with::<PdfWriter>(&spec, options),
        "hep" => render_with::<HepWriter>(&spec, options),
        "spec" | "html" | "url" | "silent" => {
            // The browser/vega-lite paths predate writer options; accepting a
            // stray option here would silently ignore it, which is worse than
            // an error.
            if !options.trim().is_empty() {
                return Err(format!(
                    "ggsql: ggsql_writer_options only apply to the 'svg', 'pdf', and 'hep' \
                     output formats (got ggsql_output = '{}')",
                    writer
                ));
            }
            run_vegalite(&spec, writer)
        }
        _ => Err(format!("ggsql: unknown writer '{}'", writer)),
    }
}

/// Parse `options` into `WriterOptions`, build the writer via its
/// `from_options`, and render. Unknown or malformed keys are rejected by ggsql
/// with a message naming the offending key, so the passthrough needs no
/// validation of its own.
fn render_with<W>(spec: &ggsql::reader::Spec, options: &str) -> Result<Vec<u8>, String>
where
    W: Writer,
    W::Output: Into<Vec<u8>>,
{
    let parsed = WriterOptions::parse([options]).map_err(|e| format!("ggsql: {}", e))?;
    let writer = W::from_options(&parsed).map_err(|e| format!("ggsql: {}", e))?;
    writer
        .render(spec)
        .map(Into::into)
        .map_err(|e| format!("ggsql: render failed: {}", e))
}

fn run_vegalite(spec: &ggsql::reader::Spec, mode: &str) -> Result<Vec<u8>, String> {
    let json = VegaLiteWriter::new()
        .render(spec)
        .map_err(|e| format!("ggsql: render failed: {}", e))?;

    match mode {
        "spec" => Ok(json.into_bytes()),
        "html" => Ok(server::standalone_html(&json).into_bytes()),
        "url" | "silent" => {
            let registered =
                server::register_spec(json).map_err(|e| format!("ggsql: serve failed: {}", e))?;

            // Only spawn a browser when the server believes no tab is currently alive.
            // An already-open tab will see the new plot via its poll loop and advance
            // in place; opening another tab on every query piles up windows. See
            // `register_spec` for the heartbeat logic. Env var still wins for tests/CI.
            if registered.should_open && std::env::var_os("GGSQL_NO_OPEN_BROWSER").is_none() {
                let _ = open::that(&registered.open_url);
            }

            match mode {
                "url" => Ok(registered.plot_url.into_bytes()),
                // Silent: the browser side-effect has happened; the C++ side will
                // suppress the row so the user sees an empty result set.
                "silent" => Ok(Vec::new()),
                _ => unreachable!(),
            }
        }
        _ => unreachable!(),
    }
}
