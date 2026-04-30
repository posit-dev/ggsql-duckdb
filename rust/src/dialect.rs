// Inlined from ggsql 0.3.1 src/reader/duckdb.rs.
// We can't enable ggsql's `duckdb` feature because that pulls in duckdb-rs with
// `bundled`, statically linking a whole second DuckDB into an extension that is
// already loaded inside DuckDB. DuckDbDialect itself has no duckdb-rs dependency,
// so we copy it here. Re-sync if ggsql changes this dialect upstream.

use ggsql::naming;
use ggsql::reader::SqlDialect;

pub struct DuckDbDialect;

impl SqlDialect for DuckDbDialect {
    fn sql_greatest(&self, exprs: &[&str]) -> String {
        if exprs.len() == 1 {
            return exprs[0].to_string();
        }
        format!("GREATEST({})", exprs.join(", "))
    }

    fn sql_least(&self, exprs: &[&str]) -> String {
        if exprs.len() == 1 {
            return exprs[0].to_string();
        }
        format!("LEAST({})", exprs.join(", "))
    }

    fn sql_generate_series(&self, n: usize) -> String {
        format!(
            "\"__ggsql_seq__\"(n) AS (SELECT generate_series FROM GENERATE_SERIES(0, {}))",
            n - 1
        )
    }

    fn sql_percentile(&self, column: &str, fraction: f64, from: &str, groups: &[String]) -> String {
        let group_filter = groups
            .iter()
            .map(|g| {
                let q = naming::quote_ident(g);
                format!(
                    "AND {pct}.{q} IS NOT DISTINCT FROM {qt}.{q}",
                    pct = naming::quote_ident("__ggsql_pct__"),
                    qt = naming::quote_ident("__ggsql_qt__")
                )
            })
            .collect::<Vec<_>>()
            .join(" ");

        let quoted_column = naming::quote_ident(column);
        format!(
            "(SELECT QUANTILE_CONT({column}, {fraction}) \
            FROM ({from}) AS \"__ggsql_pct__\" \
            WHERE {column} IS NOT NULL {group_filter})",
            column = quoted_column
        )
    }
}
