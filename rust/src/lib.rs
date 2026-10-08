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
/// `writer` names the output path: the browser display modes `silent` and
/// `url` (rendered as .hep and served over HTTP), `html` (a self-contained
/// document with the hep viewer inlined), the raw `spec` (vega-lite JSON), or
/// one of the native writers `svg`, `pdf`, `hep`. `options` is a
/// `key=value;key=value` string parsed by ggsql's `WriterOptions` and handed
/// to the native writer's `from_options`; it must be empty for `spec`.
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
    if query.is_null() || bridge.is_null() || writer.is_null() || options.is_null() || out.is_null()
    {
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
        "html" => {
            // Self-contained document: the plot as .hep with the viewer bundle
            // and wasm/font assets inlined. Same rendering as the browser
            // display, so saved HTML matches what the user sees interactively.
            let hep = render_with::<HepWriter>(&spec, options)?;
            Ok(server::standalone_html(&hep).into_bytes())
        }
        "url" | "silent" => {
            let hep = render_with::<HepWriter>(&spec, options)?;
            serve_plot(hep, writer)
        }
        "spec" => {
            // The vega-lite path predates writer options; accepting a stray
            // option here would silently ignore it, which is worse than an
            // error.
            if !options.trim().is_empty() {
                return Err(
                    "ggsql: ggsql_writer_options does not apply to the 'spec' output mode \
                     (options are for the native writers and the browser display)"
                        .to_string(),
                );
            }
            let json = VegaLiteWriter::new()
                .render(&spec)
                .map_err(|e| format!("ggsql: render failed: {}", e))?;
            Ok(json.into_bytes())
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

/// Register a .hep document with the plot server and open a browser tab if
/// none is alive. `url` returns the deep link; `silent` returns nothing (the
/// C++ side suppresses the row).
fn serve_plot(hep: Vec<u8>, mode: &str) -> Result<Vec<u8>, String> {
    let registered =
        server::register_plot(hep).map_err(|e| format!("ggsql: serve failed: {}", e))?;

    // Only spawn a browser when the server believes no tab is currently alive.
    // An already-open tab will see the new plot via its poll loop and advance
    // in place; opening another tab on every query piles up windows. See
    // `register_plot` for the heartbeat logic. Env var still wins for tests/CI.
    if registered.should_open && std::env::var_os("GGSQL_NO_OPEN_BROWSER").is_none() {
        let _ = open::that(&registered.open_url);
    }

    match mode {
        "url" => Ok(registered.plot_url.into_bytes()),
        "silent" => Ok(Vec::new()),
        _ => unreachable!(),
    }
}
