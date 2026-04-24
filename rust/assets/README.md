# Vendored vega assets

These files are served by the extension's in-process HTTP server so plots render
offline (no CDN fetches). They are baked into the extension binary via
`include_bytes!` in `rust/src/server.rs`.

| File | Package | Version | Source |
|---|---|---|---|
| `vega.min.js` | [vega](https://github.com/vega/vega) | 5.33.1 | `https://cdn.jsdelivr.net/npm/vega@5.33.1/build/vega.min.js` |
| `vega-lite.min.js` | [vega-lite](https://github.com/vega/vega-lite) | 5.23.0 | `https://cdn.jsdelivr.net/npm/vega-lite@5.23.0/build/vega-lite.min.js` |
| `vega-embed.min.js` | [vega-embed](https://github.com/vega/vega-embed) | 6.29.0 | `https://cdn.jsdelivr.net/npm/vega-embed@6.29.0/build/vega-embed.min.js` |

All three are BSD-3-Clause licensed. Re-download from the URLs above to upgrade.
