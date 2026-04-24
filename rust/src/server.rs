use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::thread;

use once_cell::sync::OnceCell;
use tiny_http::{Header, Response, Server};

static SERVER: OnceCell<ServerHandle> = OnceCell::new();

struct ServerHandle {
    base_url: String,
    specs: Arc<Mutex<HashMap<String, String>>>,
}

/// Register a vega-lite spec JSON; return the URL the browser should open.
pub fn register_spec(spec_json: String) -> Result<String, String> {
    let handle = SERVER.get_or_try_init(start_server)?;
    let id = uuid::Uuid::new_v4().to_string();
    handle
        .specs
        .lock()
        .map_err(|e| format!("spec registry poisoned: {}", e))?
        .insert(id.clone(), spec_json);
    Ok(format!("{}/plot/{}", handle.base_url, id))
}

fn start_server() -> Result<ServerHandle, String> {
    let server =
        Server::http("127.0.0.1:0").map_err(|e| format!("failed to bind http server: {}", e))?;
    let addr = server
        .server_addr()
        .to_ip()
        .ok_or("server bound to non-IP address")?;
    let base_url = format!("http://{}:{}", addr.ip(), addr.port());

    let specs: Arc<Mutex<HashMap<String, String>>> = Arc::new(Mutex::new(HashMap::new()));
    let specs_for_thread = specs.clone();

    thread::Builder::new()
        .name("ggsql-http".into())
        .spawn(move || serve_loop(server, specs_for_thread))
        .map_err(|e| format!("failed to spawn http thread: {}", e))?;

    Ok(ServerHandle { base_url, specs })
}

fn serve_loop(server: Server, specs: Arc<Mutex<HashMap<String, String>>>) {
    for request in server.incoming_requests() {
        let url = request.url().to_string();
        let response = route(&url, &specs);
        // Ignore send errors — client may have disconnected.
        let _ = request.respond(response);
    }
}

// Vendored assets — see rust/assets/README.md for versions and upgrade instructions.
// Embedded at compile time so plots render offline (no CDN fetches).
const VEGA_JS: &[u8] = include_bytes!("../assets/vega.min.js");
const VEGA_LITE_JS: &[u8] = include_bytes!("../assets/vega-lite.min.js");
const VEGA_EMBED_JS: &[u8] = include_bytes!("../assets/vega-embed.min.js");

fn route(
    url: &str,
    specs: &Arc<Mutex<HashMap<String, String>>>,
) -> Response<std::io::Cursor<Vec<u8>>> {
    if let Some(id) = url.strip_prefix("/plot/") {
        let id = id.split('?').next().unwrap_or(id).trim_end_matches('/');
        let spec = specs.lock().ok().and_then(|m| m.get(id).cloned());
        return match spec {
            Some(spec_json) => html_response(render_page(&spec_json)),
            None => not_found(),
        };
    }
    match url.split('?').next().unwrap_or(url) {
        "/assets/vega.min.js" => js_response(VEGA_JS),
        "/assets/vega-lite.min.js" => js_response(VEGA_LITE_JS),
        "/assets/vega-embed.min.js" => js_response(VEGA_EMBED_JS),
        _ => not_found(),
    }
}

fn html_response(body: String) -> Response<std::io::Cursor<Vec<u8>>> {
    let header = Header::from_bytes(&b"Content-Type"[..], &b"text/html; charset=utf-8"[..])
        .expect("static header bytes");
    Response::from_string(body).with_header(header)
}

fn js_response(body: &'static [u8]) -> Response<std::io::Cursor<Vec<u8>>> {
    let content_type =
        Header::from_bytes(&b"Content-Type"[..], &b"application/javascript; charset=utf-8"[..])
            .expect("static header bytes");
    // Let the browser cache the bundles for the life of the server. The port and server
    // identity are process-scoped so there's no upgrade path that would invalidate a stale
    // cached asset within the same run.
    let cache = Header::from_bytes(&b"Cache-Control"[..], &b"public, max-age=31536000, immutable"[..])
        .expect("static header bytes");
    Response::from_data(body.to_vec())
        .with_header(content_type)
        .with_header(cache)
}

fn not_found() -> Response<std::io::Cursor<Vec<u8>>> {
    Response::from_string("not found").with_status_code(404)
}

fn render_page(spec_json: &str) -> String {
    // vega + vega-lite + vega-embed are served locally from /assets/ (vendored, see
    // rust/assets/). No external fetches.
    // Use `r##"..."##` so the HTML can contain `"#` from e.g. `id="#vis"` without
    // terminating the raw string.
    format!(
        r##"<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>ggsql</title>
<style>
  html, body {{ margin: 0; padding: 0; height: 100%; background: #fff; font-family: system-ui, sans-serif; }}
  #vis {{ padding: 1rem; }}
</style>
<script src="/assets/vega.min.js"></script>
<script src="/assets/vega-lite.min.js"></script>
<script src="/assets/vega-embed.min.js"></script>
</head>
<body>
<div id="vis"></div>
<script id="spec" type="application/json">{spec}</script>
<script>
  const spec = JSON.parse(document.getElementById("spec").textContent);
  vegaEmbed("#vis", spec, {{ renderer: "canvas", actions: true }});
</script>
</body>
</html>"##,
        // Neutralise any `</script>` in the spec that would break out of the JSON island.
        spec = spec_json.replace("</", r"<\/"),
    )
}
