use std::io::Cursor;
use std::os::raw::c_int;
use std::sync::Arc;

use arrow::array::{RecordBatch, RecordBatchReader};
use arrow::datatypes::SchemaRef;
use arrow::error::ArrowError;
use arrow::ffi_stream::{ArrowArrayStreamReader, FFI_ArrowArrayStream};
use arrow::ipc::writer::StreamWriter;
use ggsql::reader::{execute_with_reader, Reader, Spec, SqlDialect};
use ggsql::{DataFrame, GgsqlError, Result};
use polars::prelude::*;

use crate::dialect::DuckDbDialect;
use crate::ffi::{ByteBuffer, ReaderBridge};

pub struct CallbackReader {
    bridge: ReaderBridge,
    dialect: DuckDbDialect,
}

impl CallbackReader {
    pub fn new(bridge: ReaderBridge) -> Self {
        Self { bridge, dialect: DuckDbDialect }
    }

    /// Drive the C++ callback, consume the returned Arrow C Data Interface stream,
    /// and materialise a polars DataFrame.
    //
    // TODO(ggsql-arrow-migration): drop this IPC roundtrip once ggsql ships its
    // arrow-based API (already merged on ggsql's main branch; next release will expose
    // `Reader::execute_sql -> RecordBatchIterator`). At that point we pass the
    // FFI_ArrowArrayStream straight through and skip polars entirely.
    fn exec_sql_via_bridge(&self, sql: &str) -> Result<DataFrame> {
        let mut stream = FFI_ArrowArrayStream::empty();
        let mut err = ByteBuffer::EMPTY;

        let rc = unsafe {
            (self.bridge.exec_sql)(
                self.bridge.ctx,
                sql.as_ptr() as *const _,
                sql.len(),
                &mut stream,
                &mut err,
            )
        };

        if rc != 0 {
            let msg = read_err_buffer(&self.bridge, &mut err)
                .unwrap_or_else(|| "ggsql: exec_sql callback failed with no error message".into());
            return Err(GgsqlError::ReaderError(msg));
        }

        let reader = ArrowArrayStreamReader::try_new(stream)
            .map_err(|e| GgsqlError::ReaderError(format!("arrow stream init failed: {}", e)))?;
        let schema = reader.schema();
        let batches = reader
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(|e| GgsqlError::ReaderError(format!("arrow stream read failed: {}", e)))?;

        // Re-encode as Arrow IPC stream bytes, then decode with polars. Transient.
        let mut buf: Vec<u8> = Vec::new();
        {
            let mut writer = StreamWriter::try_new(&mut buf, &schema).map_err(|e| {
                GgsqlError::ReaderError(format!("ipc writer init failed: {}", e))
            })?;
            for batch in &batches {
                writer
                    .write(batch)
                    .map_err(|e| GgsqlError::ReaderError(format!("ipc write failed: {}", e)))?;
            }
            writer
                .finish()
                .map_err(|e| GgsqlError::ReaderError(format!("ipc finish failed: {}", e)))?;
        }

        let cursor = Cursor::new(buf);
        IpcStreamReader::new(cursor).finish().map_err(|e| {
            GgsqlError::ReaderError(format!("polars ipc decode failed: {}", e))
        })
    }
}

impl Reader for CallbackReader {
    fn execute_sql(&self, sql: &str) -> Result<DataFrame> {
        self.exec_sql_via_bridge(sql)
    }

    // TODO(ggsql-register-regression): ggsql 0.2.7's engine uses `register` to stash the
    // main SELECT output as `__ggsql_global_<uuid>__` so layer queries can re-read it.
    // Upstream plan: stop abusing `register` as engine-internal staging, at which point
    // this whole roundtrip can be deleted.
    fn register(&self, name: &str, df: DataFrame, replace: bool) -> Result<()> {
        // polars DataFrame -> Arrow IPC stream bytes (polars' internal serde)
        let mut buf: Vec<u8> = Vec::new();
        {
            let mut writer = IpcStreamWriter::new(&mut buf);
            writer
                .finish(&mut df.clone())
                .map_err(|e| GgsqlError::ReaderError(format!("polars ipc write failed: {}", e)))?;
        }

        // IPC bytes -> arrow-rs RecordBatches. Needed because polars' Arrow is a fork
        // (`polars-arrow`) and can't feed arrow-rs's FFI_ArrowArrayStream directly.
        let cursor = Cursor::new(buf);
        let stream_reader = arrow::ipc::reader::StreamReader::try_new(cursor, None)
            .map_err(|e| GgsqlError::ReaderError(format!("ipc decode failed: {}", e)))?;
        let schema = stream_reader.schema();
        let batches: Vec<RecordBatch> = stream_reader
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(|e| GgsqlError::ReaderError(format!("ipc read failed: {}", e)))?;

        // Wrap in a RecordBatchReader and export as an FFI_ArrowArrayStream for C++.
        let reader: Box<dyn RecordBatchReader + Send> = Box::new(VecBatchReader {
            schema,
            iter: batches.into_iter(),
        });
        let mut stream = FFI_ArrowArrayStream::new(reader);

        let mut err = ByteBuffer::EMPTY;
        let rc = unsafe {
            (self.bridge.register_df)(
                self.bridge.ctx,
                name.as_ptr() as *const _,
                name.len(),
                &mut stream,
                if replace { 1 } else { 0 } as c_int,
                &mut err,
            )
        };

        if rc != 0 {
            let msg = read_err_buffer(&self.bridge, &mut err)
                .unwrap_or_else(|| format!("ggsql: register('{}') failed with no message", name));
            return Err(GgsqlError::ReaderError(msg));
        }
        Ok(())
    }

    fn execute(&self, query: &str) -> Result<Spec> {
        execute_with_reader(self, query)
    }

    fn dialect(&self) -> &dyn SqlDialect {
        &self.dialect
    }
}

struct VecBatchReader {
    schema: SchemaRef,
    iter: std::vec::IntoIter<RecordBatch>,
}

impl Iterator for VecBatchReader {
    type Item = std::result::Result<RecordBatch, ArrowError>;
    fn next(&mut self) -> Option<Self::Item> {
        self.iter.next().map(Ok)
    }
}

impl RecordBatchReader for VecBatchReader {
    fn schema(&self) -> SchemaRef {
        Arc::clone(&self.schema)
    }
}

fn read_err_buffer(bridge: &ReaderBridge, err: &mut ByteBuffer) -> Option<String> {
    if err.ptr.is_null() {
        return None;
    }
    let msg =
        unsafe { String::from_utf8_lossy(std::slice::from_raw_parts(err.ptr, err.len)).into_owned() };
    unsafe { (bridge.free_buffer)(err) };
    Some(msg)
}
