// C ABI types shared with the C++ side of the extension.
//
// Layout must match src/include/ggsql_ext_rs.h exactly.

use std::os::raw::{c_char, c_int, c_void};

use arrow::ffi_stream::FFI_ArrowArrayStream;

/// Byte buffer populated on one side of the FFI boundary and released on the same side.
/// Used here for UTF-8 strings (URL on success, error message on failure).
#[repr(C)]
pub struct ByteBuffer {
    pub ptr: *mut u8,
    pub len: usize,
    pub cap: usize,
}

impl ByteBuffer {
    pub const EMPTY: ByteBuffer = ByteBuffer {
        ptr: std::ptr::null_mut(),
        len: 0,
        cap: 0,
    };

    /// Move a Vec<u8> into a heap ByteBuffer. Caller frees with the matching free fn.
    pub fn from_vec(v: Vec<u8>) -> ByteBuffer {
        let mut v = std::mem::ManuallyDrop::new(v);
        ByteBuffer { ptr: v.as_mut_ptr(), len: v.len(), cap: v.capacity() }
    }
}

/// Callback: C++ runs `sql` on its inner Connection and, on success, populates
/// `out_stream` as an Arrow C Data Interface stream. On failure, populates `err_buf`.
pub type ExecSqlFn = unsafe extern "C" fn(
    ctx: *mut c_void,
    sql: *const c_char,
    sql_len: usize,
    out_stream: *mut FFI_ArrowArrayStream,
    out_err: *mut ByteBuffer,
) -> c_int;

/// Callback: C++ registers the Arrow stream as a TEMP TABLE on the inner Connection,
/// under the given name. The stream is consumed by C++; Rust must not use it afterward.
///
/// TODO(ggsql-register-regression): drop this once ggsql no longer materialises the main
/// SELECT as a named temp table inside `execute_with_reader`.
pub type RegisterDfFn = unsafe extern "C" fn(
    ctx: *mut c_void,
    name: *const c_char,
    name_len: usize,
    stream: *mut FFI_ArrowArrayStream,
    replace: c_int,
    out_err: *mut ByteBuffer,
) -> c_int;

/// Callback: C++ frees a `ByteBuffer` it previously populated.
pub type FreeBufferFn = unsafe extern "C" fn(buf: *mut ByteBuffer);

/// Bridge handle passed from C++ into Rust. `ctx` is opaque to Rust.
#[repr(C)]
pub struct ReaderBridge {
    pub ctx: *mut c_void,
    pub exec_sql: ExecSqlFn,
    pub register_df: RegisterDfFn,
    pub free_buffer: FreeBufferFn,
}
