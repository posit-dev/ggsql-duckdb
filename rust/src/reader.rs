use arrow::array::{RecordBatch, RecordBatchReader};
use arrow::compute::concat_batches;
use arrow::ffi_stream::{ArrowArrayStreamReader, FFI_ArrowArrayStream};
use ggsql::reader::{execute_with_reader, Reader, Spec, SqlDialect};
use ggsql::{DataFrame, GgsqlError, Result};

use crate::dialect::DuckDbDialect;
use crate::ffi::{ByteBuffer, ReaderBridge};

pub struct CallbackReader {
    bridge: ReaderBridge,
    dialect: DuckDbDialect,
}

impl CallbackReader {
    pub fn new(bridge: ReaderBridge) -> Self {
        Self {
            bridge,
            dialect: DuckDbDialect,
        }
    }

    /// Drive the C++ callback, consume the returned Arrow C Data Interface stream,
    /// and wrap it as a ggsql `DataFrame` (single concatenated `RecordBatch`).
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
        let batches: Vec<RecordBatch> = reader
            .collect::<std::result::Result<Vec<_>, _>>()
            .map_err(|e| GgsqlError::ReaderError(format!("arrow stream read failed: {}", e)))?;

        let batch = if batches.is_empty() {
            RecordBatch::new_empty(schema)
        } else {
            concat_batches(&schema, &batches)
                .map_err(|e| GgsqlError::ReaderError(format!("arrow concat_batches failed: {}", e)))?
        };

        Ok(DataFrame::from_record_batch(batch))
    }
}

impl Reader for CallbackReader {
    fn execute_sql(&self, sql: &str) -> Result<DataFrame> {
        self.exec_sql_via_bridge(sql)
    }

    // ggsql 0.3.0's engine no longer calls `register` on the visualise path — CTEs and
    // the global query are materialised via `execute_sql(create_or_replace_temp_table_sql(...))`
    // instead. The trait method is still required, so we keep a stub that surfaces a
    // clean error if any future code path does invoke it.
    fn register(&self, _name: &str, _df: DataFrame, _replace: bool) -> Result<()> {
        Err(GgsqlError::ReaderError(
            "ggsql-duckdb reader does not implement register; ggsql 0.3.0 should not call it"
                .into(),
        ))
    }

    fn execute(&self, query: &str) -> Result<Spec> {
        execute_with_reader(self, query)
    }

    fn dialect(&self) -> &dyn SqlDialect {
        &self.dialect
    }
}

fn read_err_buffer(bridge: &ReaderBridge, err: &mut ByteBuffer) -> Option<String> {
    if err.ptr.is_null() {
        return None;
    }
    let msg = unsafe {
        String::from_utf8_lossy(std::slice::from_raw_parts(err.ptr, err.len)).into_owned()
    };
    unsafe { (bridge.free_buffer)(err) };
    Some(msg)
}
