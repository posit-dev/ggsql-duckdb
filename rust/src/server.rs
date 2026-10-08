use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use base64::Engine;
use once_cell::sync::OnceCell;
use tiny_http::{Header, Response, Server};

static SERVER: OnceCell<ServerHandle> = OnceCell::new();

/// Poll freshness threshold. The SPA hits /api/latest every 500ms, so any silence
/// longer than this strongly suggests the tab was closed (or heavily throttled).
const TAB_ALIVE_WINDOW: Duration = Duration::from_secs(5);

struct State {
    plots: Mutex<HashMap<String, Vec<u8>>>,
    latest: Mutex<Option<String>>,
    // Last time a client pinged /api/latest. Used as a "tab alive" heartbeat — the
    // SPA's existing poll loop doubles as liveness signal without a separate endpoint.
    last_poll: Mutex<Option<Instant>>,
}

struct ServerHandle {
    base_url: String,
    state: Arc<State>,
}

/// Returned from [`register_plot`]. `plot_url` is the stable, per-plot URL (shareable,
/// deep-linkable). `open_url` is what should be handed to the OS-level `open`. The
/// caller should only spawn a browser when `should_open` is true — otherwise the
/// existing tab will pick up the new plot via its poll loop and spawning another one
/// just leaves an annoying trail of windows.
pub struct Registered {
    pub plot_url: String,
    pub open_url: String,
    pub should_open: bool,
}

/// Register a .hep plot document; return URLs for display and browser-open.
pub fn register_plot(hep: Vec<u8>) -> Result<Registered, String> {
    let handle = SERVER.get_or_try_init(start_server)?;
    let id = uuid::Uuid::new_v4().to_string();

    {
        let mut plots = handle
            .state
            .plots
            .lock()
            .map_err(|e| format!("plot registry poisoned: {}", e))?;
        plots.insert(id.clone(), hep);
    }
    {
        let mut latest = handle
            .state
            .latest
            .lock()
            .map_err(|e| format!("latest pointer poisoned: {}", e))?;
        *latest = Some(id.clone());
    }

    // Open a browser only if we haven't seen a recent poll from the SPA. If the tab
    // is alive, its next poll (within ~500ms) will pick up the new plot and advance
    // in place via history.pushState — no `open::that` needed.
    let should_open = handle
        .state
        .last_poll
        .lock()
        .ok()
        .and_then(|g| *g)
        .map_or(true, |t| t.elapsed() > TAB_ALIVE_WINDOW);

    Ok(Registered {
        plot_url: format!("{}/#plot/{}", handle.base_url, id),
        open_url: format!("{}/", handle.base_url),
        should_open,
    })
}

fn mark_poll(state: &State) {
    if let Ok(mut g) = state.last_poll.lock() {
        *g = Some(Instant::now());
    }
}

fn start_server() -> Result<ServerHandle, String> {
    let server =
        Server::http("127.0.0.1:0").map_err(|e| format!("failed to bind http server: {}", e))?;
    let addr = server
        .server_addr()
        .to_ip()
        .ok_or("server bound to non-IP address")?;
    let base_url = format!("http://{}:{}", addr.ip(), addr.port());

    let state = Arc::new(State {
        plots: Mutex::new(HashMap::new()),
        latest: Mutex::new(None),
        last_poll: Mutex::new(None),
    });
    let state_for_thread = Arc::clone(&state);

    thread::Builder::new()
        .name("ggsql-http".into())
        .spawn(move || serve_loop(server, state_for_thread))
        .map_err(|e| format!("failed to spawn http thread: {}", e))?;

    Ok(ServerHandle { base_url, state })
}

fn serve_loop(server: Server, state: Arc<State>) {
    for request in server.incoming_requests() {
        let url = request.url().to_string();
        let response = route(&url, &state);
        // Ignore send errors — client may have disconnected.
        let _ = request.respond(response);
    }
}

// Vendored assets — see rust/assets/README.md for provenance and upgrade
// instructions. Embedded at compile time so plots render offline (no CDN
// fetches). hep-assets.js sets globalThis.ggsqlHepAssets (the wasm binary and
// Roboto faces, gzip+base64); hep-viewer.js is the IIFE viewer that consumes
// it. Both also inline into self-contained HTML output.
const HEP_ASSETS_JS: &str = include_str!("../assets/hep-assets.js");
const HEP_VIEWER_JS: &str = include_str!("../assets/hep-viewer.js");

fn route(url: &str, state: &Arc<State>) -> Response<std::io::Cursor<Vec<u8>>> {
    // Strip query string for path matching.
    let path = url.split('?').next().unwrap_or(url);

    // Static assets.
    match path {
        "/assets/hep-assets.js" => return js_response(HEP_ASSETS_JS.as_bytes()),
        "/assets/hep-viewer.js" => return js_response(HEP_VIEWER_JS.as_bytes()),
        _ => {}
    }

    // JSON API.
    if path == "/api/latest" {
        // Treat every /api/latest hit as a heartbeat — so register_plot can tell
        // whether an existing browser tab is still alive.
        mark_poll(state);
        let latest = state.latest.lock().ok().and_then(|g| g.clone());
        let body = match latest {
            Some(uuid) => format!("{{\"uuid\":\"{}\"}}", uuid),
            None => "{\"uuid\":null}".to_string(),
        };
        return json_response(body);
    }
    if let Some(rest) = path.strip_prefix("/api/plot/") {
        let id = rest.trim_end_matches('/');
        let plot = state.plots.lock().ok().and_then(|m| m.get(id).cloned());
        return match plot {
            Some(bytes) => binary_response(bytes),
            None => not_found(),
        };
    }

    // Page routes — the SPA shell handles the display logic based on window.location.
    if path == "/" || path.starts_with("/plot/") {
        return html_response(app_shell());
    }
    not_found()
}

fn html_response(body: String) -> Response<std::io::Cursor<Vec<u8>>> {
    let header = Header::from_bytes(&b"Content-Type"[..], &b"text/html; charset=utf-8"[..])
        .expect("static header bytes");
    Response::from_string(body).with_header(header)
}

fn json_response(body: String) -> Response<std::io::Cursor<Vec<u8>>> {
    let content_type = Header::from_bytes(
        &b"Content-Type"[..],
        &b"application/json; charset=utf-8"[..],
    )
    .expect("static header bytes");
    let cache =
        Header::from_bytes(&b"Cache-Control"[..], &b"no-store"[..]).expect("static header bytes");
    Response::from_string(body)
        .with_header(content_type)
        .with_header(cache)
}

fn js_response(body: &[u8]) -> Response<std::io::Cursor<Vec<u8>>> {
    let content_type = Header::from_bytes(
        &b"Content-Type"[..],
        &b"application/javascript; charset=utf-8"[..],
    )
    .expect("static header bytes");
    // The bundles are immutable for the life of the server process.
    let cache = Header::from_bytes(
        &b"Cache-Control"[..],
        &b"public, max-age=31536000, immutable"[..],
    )
    .expect("static header bytes");
    Response::from_data(body.to_vec())
        .with_header(content_type)
        .with_header(cache)
}

fn binary_response(body: Vec<u8>) -> Response<std::io::Cursor<Vec<u8>>> {
    let content_type = Header::from_bytes(
        &b"Content-Type"[..],
        &b"application/octet-stream"[..],
    )
    .expect("static header bytes");
    let cache =
        Header::from_bytes(&b"Cache-Control"[..], &b"no-store"[..]).expect("static header bytes");
    Response::from_data(body)
        .with_header(content_type)
        .with_header(cache)
}

fn not_found() -> Response<std::io::Cursor<Vec<u8>>> {
    Response::from_string("not found").with_status_code(404)
}

// The single-page app shell. No plot is inlined; the client reads its own URL, fetches
// the .hep document from /api/plot/<uuid>, and polls /api/latest so that new plots
// appear in the same tab. history.pushState gives us working back/forward.
fn app_shell() -> String {
    include_str!("../assets/app.html").to_string()
}

/// Build a fully self-contained HTML document from a .hep plot document: the
/// viewer bundle and the wasm/font assets inlined (both already ASCII-safe
/// JS/base64), plus the plot itself as base64. No network needed to render.
/// Used by `ggsql_output = 'html'` and by `ggsql_save(…, 'plot.html')`.
pub fn standalone_html(hep: &[u8]) -> String {
    // Base64 is drawn from [A-Za-z0-9+/=], so the payload can never contain a
    // `</script>` breakout sequence.
    let doc_b64 = base64::engine::general_purpose::STANDARD.encode(hep);
    format!(
        r##"<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>ggsql</title>
<style>
  html, body {{ margin: 0; padding: 0; height: 100%; background: #fff; font-family: system-ui, sans-serif; }}
  body {{ display: flex; flex-direction: column; height: 100vh; }}
  /* PlotView sizes to its container via a ResizeObserver — give it a flex:1
     child of the body so it fills the viewport. */
  #vis {{ flex: 1; min-height: 0; padding: 1rem; box-sizing: border-box; }}
</style>
<script>{assets}</script>
<script>{viewer}</script>
</head>
<body>
<div id="vis"></div>
<script>
  const bytes = Uint8Array.from(atob("{doc}"), (c) => c.charCodeAt(0));
  window.ggsqlRenderHep(document.getElementById("vis"), bytes);
</script>
</body>
</html>"##,
        assets = HEP_ASSETS_JS,
        viewer = HEP_VIEWER_JS,
        doc = doc_b64,
    )
}
