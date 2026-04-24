use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use once_cell::sync::OnceCell;
use tiny_http::{Header, Response, Server};

static SERVER: OnceCell<ServerHandle> = OnceCell::new();

/// Poll freshness threshold. The SPA hits /api/latest every 500ms, so any silence
/// longer than this strongly suggests the tab was closed (or heavily throttled).
const TAB_ALIVE_WINDOW: Duration = Duration::from_secs(5);

struct State {
    specs: Mutex<HashMap<String, String>>,
    latest: Mutex<Option<String>>,
    // Last time a client pinged /api/latest. Used as a "tab alive" heartbeat — the
    // SPA's existing poll loop doubles as liveness signal without a separate endpoint.
    last_poll: Mutex<Option<Instant>>,
}

struct ServerHandle {
    base_url: String,
    state: Arc<State>,
}

/// Returned from [`register_spec`]. `plot_url` is the stable, per-plot URL (shareable,
/// deep-linkable). `open_url` is what should be handed to the OS-level `open`. The
/// caller should only spawn a browser when `should_open` is true — otherwise the
/// existing tab will pick up the new plot via its poll loop and spawning another one
/// just leaves an annoying trail of windows.
pub struct Registered {
    pub plot_url: String,
    pub open_url: String,
    pub should_open: bool,
}

/// Register a vega-lite spec JSON; return URLs for display and browser-open.
pub fn register_spec(spec_json: String) -> Result<Registered, String> {
    let handle = SERVER.get_or_try_init(start_server)?;
    let id = uuid::Uuid::new_v4().to_string();

    {
        let mut specs = handle
            .state
            .specs
            .lock()
            .map_err(|e| format!("spec registry poisoned: {}", e))?;
        specs.insert(id.clone(), spec_json);
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
        specs: Mutex::new(HashMap::new()),
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

// Vendored assets — see rust/assets/README.md for versions and upgrade instructions.
// Embedded at compile time so plots render offline (no CDN fetches).
// Kept as &str (not bytes) so they can also be inlined into self-contained HTML output.
const VEGA_JS: &str = include_str!("../assets/vega.min.js");
const VEGA_LITE_JS: &str = include_str!("../assets/vega-lite.min.js");
const VEGA_EMBED_JS: &str = include_str!("../assets/vega-embed.min.js");

fn route(url: &str, state: &Arc<State>) -> Response<std::io::Cursor<Vec<u8>>> {
    // Strip query string for path matching.
    let path = url.split('?').next().unwrap_or(url);

    // Static assets.
    match path {
        "/assets/vega.min.js" => return js_response(VEGA_JS.as_bytes()),
        "/assets/vega-lite.min.js" => return js_response(VEGA_LITE_JS.as_bytes()),
        "/assets/vega-embed.min.js" => return js_response(VEGA_EMBED_JS.as_bytes()),
        _ => {}
    }

    // JSON API.
    if path == "/api/latest" {
        // Treat every /api/latest hit as a heartbeat — so register_spec can tell
        // whether an existing browser tab is still alive.
        mark_poll(state);
        let latest = state.latest.lock().ok().and_then(|g| g.clone());
        let body = match latest {
            Some(uuid) => format!("{{\"uuid\":\"{}\"}}", uuid),
            None => "{\"uuid\":null}".to_string(),
        };
        return json_response(body);
    }
    if let Some(rest) = path.strip_prefix("/api/spec/") {
        let id = rest.trim_end_matches('/');
        let spec = state.specs.lock().ok().and_then(|m| m.get(id).cloned());
        return match spec {
            Some(body) => json_response(body),
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
    let content_type =
        Header::from_bytes(&b"Content-Type"[..], &b"application/json; charset=utf-8"[..])
            .expect("static header bytes");
    let cache = Header::from_bytes(&b"Cache-Control"[..], &b"no-store"[..])
        .expect("static header bytes");
    Response::from_string(body).with_header(content_type).with_header(cache)
}

fn js_response(body: &[u8]) -> Response<std::io::Cursor<Vec<u8>>> {
    let content_type =
        Header::from_bytes(&b"Content-Type"[..], &b"application/javascript; charset=utf-8"[..])
            .expect("static header bytes");
    // The bundles are immutable for the life of the server process.
    let cache = Header::from_bytes(&b"Cache-Control"[..], &b"public, max-age=31536000, immutable"[..])
        .expect("static header bytes");
    Response::from_data(body.to_vec()).with_header(content_type).with_header(cache)
}

fn not_found() -> Response<std::io::Cursor<Vec<u8>>> {
    Response::from_string("not found").with_status_code(404)
}

// The single-page app shell. No spec is inlined; the client reads its own URL, fetches
// the spec from /api/spec/<uuid>, and polls /api/latest so that new plots appear in the
// same tab. history.pushState gives us working back/forward.
fn app_shell() -> String {
    include_str!("../assets/app.html").to_string()
}

/// Build a fully self-contained HTML document: vega + vega-lite + vega-embed inlined
/// from the vendored bundles, plus the spec embedded as JSON. No network needed to
/// render. Used by `ggsql_output = 'html'`.
///
/// Safety note: none of the vendored minified bundles contain the literal byte sequence
/// `</script`, so inlining them inside `<script>…</script>` is safe. A re-bundle that
/// breaks that invariant would be caught at build time if we add a test; for now, see
/// the check in rust/assets/README.md.
pub fn standalone_html(spec_json: &str) -> String {
    format!(
        r##"<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>ggsql</title>
<style>
  html, body {{ margin: 0; padding: 0; height: 100%; background: #fff; font-family: system-ui, sans-serif; }}
  body {{ display: flex; flex-direction: column; height: 100vh; }}
  /* ggsql emits width:"container" / height:"container" — the parent must have explicit
     dimensions. Make #vis a flex:1 child of the body so it fills the viewport. */
  #vis {{ flex: 1; min-height: 0; padding: 1rem; box-sizing: border-box; }}
</style>
<script>{vega}</script>
<script>{vl}</script>
<script>{embed}</script>
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
        vega = VEGA_JS,
        vl = VEGA_LITE_JS,
        embed = VEGA_EMBED_JS,
        // Neutralise any `</script>` in the spec that would break out of the JSON island.
        spec = spec_json.replace("</", r"<\/"),
    )
}
