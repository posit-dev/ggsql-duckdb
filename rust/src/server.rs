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
    not_found()
}

fn html_response(body: String) -> Response<std::io::Cursor<Vec<u8>>> {
    let header = Header::from_bytes(&b"Content-Type"[..], &b"text/html; charset=utf-8"[..])
        .expect("static header bytes");
    Response::from_string(body).with_header(header)
}

fn not_found() -> Response<std::io::Cursor<Vec<u8>>> {
    Response::from_string("not found").with_status_code(404)
}

fn render_page(spec_json: &str) -> String {
    // vega-embed loaded from jsDelivr. Offline bundling is a follow-up.
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
<script src="https://cdn.jsdelivr.net/npm/vega@5"></script>
<script src="https://cdn.jsdelivr.net/npm/vega-lite@5"></script>
<script src="https://cdn.jsdelivr.net/npm/vega-embed@6"></script>
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
