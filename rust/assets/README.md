# Vendored vega assets

These files are served by the extension's in-process HTTP server so plots render
offline (no CDN fetches). They are baked into the extension binary via
`include_str!` in `rust/src/server.rs`.

| File | Package | Version | Source |
|---|---|---|---|
| `vega.min.js` | [vega](https://github.com/vega/vega) | 6.4.0 | `https://registry.npmjs.org/vega/-/vega-6.4.0.tgz` |
| `vega-lite.min.js` | [vega-lite](https://github.com/vega/vega-lite) | 6.4.3 | `https://registry.npmjs.org/vega-lite/-/vega-lite-6.4.3.tgz` |
| `vega-embed.min.js` | [vega-embed](https://github.com/vega/vega-embed) | 7.3.0 | `https://registry.npmjs.org/vega-embed/-/vega-embed-7.3.0.tgz` |

These are the unmodified `package/build/*.min.js` bundles from the official npm
packages. Their BSD-3-Clause license notices are retained in the corresponding
`*.LICENSE` files.

ggsql 0.5.2 emits Vega-Lite v6 specifications. Vega-Lite 6.4.3 requires Vega
`^6.0.0`, and Vega-Embed 7.3.0 is developed against Vega `^6.4.0` and Vega-Lite
`^6.4.3`. Keep these versions compatible when updating the Rust dependency.

Before replacing a bundle, verify it contains no case-insensitive `</script`
sequence: HTML mode embeds these files directly inside script elements. All
three pinned bundles pass this check. Keep the filenames above unchanged so the
SPA routes and standalone HTML continue to use the same vendored assets.
