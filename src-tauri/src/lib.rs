use std::ffi::{CStr, CString, c_char, c_void};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use serde_json::{json, Value};
use tauri::{Emitter, Manager};
struct EventNote { name: String, json: String }
#[repr(C)]
struct SchemeOut { status: i32, headers_json: *mut c_char, body: *mut c_void, body_len: usize }
static EVENT_TX: Mutex<Option<tauri::async_runtime::Sender<EventNote>>> = Mutex::new(None);
static WINDOW_READY: AtomicBool = AtomicBool::new(false);
unsafe extern "C" {
    fn pavillion_api_version() -> i32;
    fn pavillion_start(workdir: *const c_char, err: *mut c_char, err_len: usize) -> i32;
    fn pavillion_stop();
    fn pavillion_call(cmd: *const c_char, args_json: *const c_char, result_json: *mut *mut c_char) -> i32;
    fn pavillion_free(p: *mut c_void);
    fn pav_on_event(cb: Option<unsafe extern "C" fn(*const c_char, *const c_char, *mut c_void)>, user: *mut c_void);
    fn pav_protocol(req_json: *const c_char, out: *mut SchemeOut) -> i32;
    fn pav_cli_main(argc: i32, argv: *mut *mut c_char) -> i32;
}
pub fn unknown_subcommand() -> i32 {
    unsafe { pav_cli_main(0, std::ptr::null_mut()) }
}
unsafe extern "C" fn on_core_event(name: *const c_char, json_text: *const c_char, _user: *mut c_void) {
    if name.is_null() { return; }
    let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned();
    let body = if json_text.is_null() { "{}".to_string() } else { unsafe { CStr::from_ptr(json_text) }.to_string_lossy().into_owned() };
    if let Some(tx) = EVENT_TX.lock().unwrap_or_else(|err| err.into_inner()).as_ref() {
        let _ = tx.try_send(EventNote { name, json: body });
    }
}
fn fail(code: &str, message: &str) -> Value {
    json!({"code": code, "message": message, "retryable": false})
}
fn err_str<'a>(err: Option<&'a Value>, key: &str, fallback: &'a str) -> &'a str {
    err.and_then(|item| item.get(key)).and_then(Value::as_str).unwrap_or(fallback)
}
fn core_data(cmd: &str, args: &Value) -> Result<Value, Value> {
    let args_text = serde_json::to_string(args).map_err(|_| fail("internal", "cannot encode arguments"))?;
    let c_cmd = CString::new(cmd).map_err(|_| fail("bad_args", "command contains NUL"))?;
    let c_args = CString::new(args_text).map_err(|_| fail("bad_args", "arguments contain NUL"))?;
    let mut result: *mut c_char = std::ptr::null_mut();
    if unsafe { pavillion_call(c_cmd.as_ptr(), c_args.as_ptr(), &mut result) } != 0 || result.is_null() {
        return Err(fail("internal", "core produced no reply"));
    }
    let text = unsafe { CStr::from_ptr(result) }.to_string_lossy().into_owned();
    unsafe { pavillion_free(result.cast()) };
    let reply: Value = serde_json::from_str(&text).map_err(|_| fail("internal", "reply is not JSON"))?;
    if reply.get("ok").and_then(Value::as_bool) == Some(true) {
        return Ok(reply.get("data").cloned().unwrap_or(Value::Null));
    }
    let err = reply.get("error");
    Err(json!({
        "code": err_str(err, "code", "internal"),
        "message": err_str(err, "message", "core call failed"),
        "retryable": err.and_then(|item| item.get("retryable")).and_then(Value::as_bool).unwrap_or(false),
    }))
}
fn logical(physical: f64, scale: f64) -> i64 { (physical / scale).round() as i64 }
fn monitor_list(window: &tauri::WebviewWindow) -> Value {
    let Ok(list) = window.available_monitors() else { return json!([]) };
    let mut out = Vec::new();
    for monitor in list {
        let scale = monitor.scale_factor();
        if scale <= 0.0 { continue; }
        let x = logical(monitor.position().x as f64, scale);
        let y = logical(monitor.position().y as f64, scale);
        let w = logical(monitor.size().width as f64, scale);
        let h = logical(monitor.size().height as f64, scale);
        if (1..=32000).contains(&w) && (1..=32000).contains(&h) && (-32000..=32000).contains(&x) && (-32000..=32000).contains(&y) {
            out.push(json!({"x": x, "y": y, "w": w, "h": h}));
        }
    }
    Value::Array(out)
}

fn report_window(window: &tauri::WebviewWindow) {
    let Ok(scale) = window.scale_factor() else { return };
    if scale <= 0.0 { return }
    let (Ok(size), Ok(pos)) = (window.inner_size(), window.outer_position()) else { return };
    let (w, h) = (logical(size.width as f64, scale), logical(size.height as f64, scale));
    let (x, y) = (logical(pos.x as f64, scale), logical(pos.y as f64, scale));
    if !(200..=10000).contains(&w) || !(200..=10000).contains(&h) || !(-32000..=32000).contains(&x) || !(-32000..=32000).contains(&y) {
        return;
    }
    let args = json!({"x": x, "y": y, "w": w, "h": h, "maximized": window.is_maximized().unwrap_or(false), "monitors": monitor_list(window)});
    if let Err(err) = core_data("window.report", &args) {
        eprintln!("cannot report window: {}", err_str(Some(&err), "message", "core call failed"));
    }
}

fn apply_initial(window: &tauri::WebviewWindow) {
    let data = match core_data("window.initial", &json!({"monitors": monitor_list(window)})) {
        Ok(data) => data,
        Err(err) => {
            eprintln!("cannot read window: {}", err_str(Some(&err), "message", "core call failed"));
            let _ = window.show();
            return;
        }
    };
    let w = data.get("w").and_then(Value::as_i64).unwrap_or(0);
    let h = data.get("h").and_then(Value::as_i64).unwrap_or(0);
    if w >= 200 && h >= 200 {
        let _ = window.set_size(tauri::Size::Logical(tauri::LogicalSize::new(w as f64, h as f64)));
    }
    if let (Some(x), Some(y)) = (data.get("x").and_then(Value::as_i64), data.get("y").and_then(Value::as_i64)) {
        let _ = window.set_position(tauri::Position::Logical(tauri::LogicalPosition::new(x as f64, y as f64)));
    }
    if data.get("maximized").and_then(Value::as_bool) == Some(true) { let _ = window.maximize(); }
    let _ = window.show();
    WINDOW_READY.store(true, Ordering::SeqCst);
}

#[tauri::command]
async fn core(cmd: String, args: Value) -> Result<Value, Value> {
    tauri::async_runtime::spawn_blocking(move || core_data(&cmd, &args)).await.unwrap_or_else(|_| Err(fail("internal", "command task failed")))
}
fn require_api_version() {
    let core_version = unsafe { pavillion_api_version() };
    let shell: i32 = env!("PAVILLION_API_VERSION").parse().expect("PAVILLION_API_VERSION from build.rs");
    if core_version != shell {
        eprintln!("API version mismatch: core={core_version} shell={shell}");
        std::process::exit(1);
    }
}
fn start_core(workdir: &str) {
    require_api_version();
    let Ok(path) = CString::new(workdir) else { eprintln!("workdir contains a NUL"); std::process::exit(1); };
    let mut err = [0u8; 512];
    if unsafe { pavillion_start(path.as_ptr(), err.as_mut_ptr().cast(), err.len()) } != 0 {
        eprintln!("pavillion_start failed: {}", unsafe { CStr::from_ptr(err.as_ptr().cast()) }.to_string_lossy());
    }
}

fn stop_events() {
    unsafe { pav_on_event(None, std::ptr::null_mut()) };
    EVENT_TX.lock().unwrap_or_else(|err| err.into_inner()).take();
}

fn start_events(app: &tauri::AppHandle) {
    let (tx, mut rx) = tauri::async_runtime::channel::<EventNote>(64);
    *EVENT_TX.lock().unwrap_or_else(|err| err.into_inner()) = Some(tx);
    unsafe { pav_on_event(Some(on_core_event), std::ptr::null_mut()) };
    let handle = app.clone();
    tauri::async_runtime::spawn(async move {
        while let Some(note) = rx.recv().await {
            let payload = serde_json::from_str::<Value>(&note.json).unwrap_or(Value::String(note.json));
            if let Err(err) = handle.emit("pav", json!({"name": note.name, "payload": payload})) {
                eprintln!("cannot emit pav: {err}");
            }
        }
    });
}

fn scheme_reply(scheme: &str, request: &tauri::http::Request<Vec<u8>>) -> tauri::http::Response<Vec<u8>> {
    let empty = || tauri::http::Response::builder().status(500).body(Vec::new()).unwrap_or_else(|_| tauri::http::Response::new(Vec::new()));
    let range = request.headers().get(tauri::http::header::RANGE).and_then(|value| value.to_str().ok()).unwrap_or("");
    let Ok(text) = serde_json::to_string(&json!({"scheme": scheme, "url": request.uri().to_string(), "method": request.method().as_str(), "range": range})) else { return empty() };
    let Ok(c_text) = CString::new(text) else { return empty() };
    let mut out = SchemeOut { status: 500, headers_json: std::ptr::null_mut(), body: std::ptr::null_mut(), body_len: 0 };
    if unsafe { pav_protocol(c_text.as_ptr(), &mut out) } != 0 { return empty() }
    let body = if out.body.is_null() || out.body_len == 0 { Vec::new() } else { unsafe { std::slice::from_raw_parts(out.body.cast::<u8>(), out.body_len) }.to_vec() };
    let headers_text = if out.headers_json.is_null() { String::new() } else { unsafe { CStr::from_ptr(out.headers_json) }.to_string_lossy().into_owned() };
    unsafe { pavillion_free(out.headers_json.cast()); pavillion_free(out.body); }
    let mut builder = tauri::http::Response::builder().status(out.status as u16);
    if let Ok(Value::Object(map)) = serde_json::from_str::<Value>(&headers_text) {
        for (key, value) in map { if let Some(text) = value.as_str() { builder = builder.header(key, text); } }
    }
    builder.body(body).unwrap_or_else(|_| empty())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run(workdir: &str) {
    start_core(workdir);
    let mut builder = tauri::Builder::default().invoke_handler(tauri::generate_handler![core]);
    for scheme in ["pav-blob", "pav-cid", "pav-link"] {
        let name = scheme.to_string();
        builder = builder.register_uri_scheme_protocol(scheme, move |_ctx, request| scheme_reply(&name, &request));
    }
    builder.setup(|app| {
        start_events(app.handle());
        let Some(window) = app.get_webview_window("main") else { eprintln!("main window is missing"); return Ok(()) };
        let watch = window.clone();
        window.on_window_event(move |event| {
            if WINDOW_READY.load(Ordering::SeqCst) && matches!(event, tauri::WindowEvent::Resized(_) | tauri::WindowEvent::Moved(_)) {
                report_window(&watch);
            }
        });
        apply_initial(&window);
        Ok(())
    }).build(tauri::generate_context!()).expect("error while building pavillion").run(|app, event| match event {
        tauri::RunEvent::ExitRequested { .. } => {
            WINDOW_READY.store(false, Ordering::SeqCst);
            if let Some(window) = app.get_webview_window("main") { report_window(&window); }
        }
        tauri::RunEvent::Exit => { stop_events(); unsafe { pavillion_stop() }; }
        _ => {}
    });
}
