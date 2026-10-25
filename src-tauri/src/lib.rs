use std::ffi::{CStr, CString, c_char, c_void};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::Mutex;
use serde_json::{json, Value};
use tauri::{Emitter, Listener, Manager};
struct EventNote { name: String, json: String }
#[repr(C)]
struct SchemeOut { status: i32, headers_json: *mut c_char, body: *mut c_void, body_len: usize }
static EVENT_TX: Mutex<Option<tauri::async_runtime::Sender<EventNote>>> = Mutex::new(None);
static WINDOW_READY: AtomicBool = AtomicBool::new(false);
/* Shown only after the saved rectangle is applied and the cover is laid out.
   A missed signal still shows the window. */
static WINDOW_PLACED: AtomicBool = AtomicBool::new(false);
static WINDOW_PAINTED: AtomicBool = AtomicBool::new(false);
static WINDOW_SHOWN: AtomicBool = AtomicBool::new(false);
#[used]
static STRONGHOLD_KEEP: (
    unsafe extern "C" fn(*const c_char, *const u8, usize, *const c_char, *const c_char, *const c_char) -> i32,
    unsafe extern "C" fn(*const c_char, *const u8, usize, *const c_char, *mut *mut c_char) -> i32,
    unsafe extern "C" fn(*const c_char, *const u8, usize, *const c_char) -> i32,
) = (
    pav_stronghold::pav_stronghold_set,
    pav_stronghold::pav_stronghold_get,
    pav_stronghold::pav_stronghold_delete,
);
unsafe extern "C" {
    fn pavillion_api_version() -> i32;
    fn pavillion_start(workdir: *const c_char, err: *mut c_char, err_len: usize) -> i32;
    fn pavillion_stop();
    fn pavillion_call(cmd: *const c_char, args_json: *const c_char, result_json: *mut *mut c_char) -> i32;
    fn pavillion_free(p: *mut c_void);
    fn pav_on_event(cb: Option<unsafe extern "C" fn(*const c_char, *const c_char, *mut c_void)>, user: *mut c_void);
    fn pav_protocol(req_json: *const c_char, out: *mut SchemeOut) -> i32;
    fn pav_cli_main(argc: i32, argv: *mut *mut c_char) -> i32;
    fn pav_ui_log(subsystem: *const c_char, level: i32, message: *const c_char) -> i32;
}
/// H-4: shell lines go through the core's pav_log (1 = WARN, 0 = ERROR).
/// Safe before pavillion_start: the core keeps them and copies WARN and
/// ERROR to stderr until a profile attaches.
pub fn core_log(level: i32, message: &str) {
    let text = CString::new(message.replace('\0', " ")).unwrap_or_default();
    unsafe { pav_ui_log(c"shell".as_ptr(), level, text.as_ptr()); }
}
macro_rules! shell_warn { ($($arg:tt)*) => { core_log(1, &format!($($arg)*)) }; }
macro_rules! shell_error { ($($arg:tt)*) => { core_log(0, &format!($($arg)*)) }; }
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
    let rc = unsafe { pavillion_call(c_cmd.as_ptr(), c_args.as_ptr(), &mut result) };
    // K Lock: the arguments may carry a typed password; wipe the copy now.
    zeroize::Zeroize::zeroize(&mut c_args.into_bytes());
    if rc != 0 || result.is_null() {
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
        shell_warn!("cannot report window: {}", err_str(Some(&err), "message", "core call failed"));
    }
}

fn reveal_window(window: &tauri::WebviewWindow) {
    if WINDOW_SHOWN.swap(true, Ordering::SeqCst) { return; }
    let _ = window.show();
    WINDOW_READY.store(true, Ordering::SeqCst);
}
fn try_reveal(window: &tauri::WebviewWindow) {
    if WINDOW_PLACED.load(Ordering::SeqCst) && WINDOW_PAINTED.load(Ordering::SeqCst) {
        reveal_window(window);
    }
}
fn note_splash_painted(window: &tauri::WebviewWindow) {
    WINDOW_PAINTED.store(true, Ordering::SeqCst);
    try_reveal(window);
}
fn arm_reveal_timeout(window: tauri::WebviewWindow) {
    let built = std::thread::Builder::new().name("splash-reveal".into()).stack_size(512 * 1024).spawn(move || {
        std::thread::sleep(std::time::Duration::from_secs(2));
        if WINDOW_SHOWN.load(Ordering::SeqCst) { return; }
        shell_warn!("splash did not paint; showing the window");
        WINDOW_PAINTED.store(true, Ordering::SeqCst);
        try_reveal(&window);
    });
    if built.is_err() { shell_warn!("cannot wait for the splash"); }
}
fn apply_initial(window: &tauri::WebviewWindow) {
    let data = match core_data("window.initial", &json!({"monitors": monitor_list(window)})) {
        Ok(data) => data,
        Err(err) => {
            shell_warn!("cannot read window: {}", err_str(Some(&err), "message", "core call failed"));
            WINDOW_PLACED.store(true, Ordering::SeqCst);
            try_reveal(window);
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
    WINDOW_PLACED.store(true, Ordering::SeqCst);
    try_reveal(window);
}

fn writes_clean_session(cmd: &str, args: &Value) -> bool {
    cmd == "prefs.ui.set"
        && args.get("session").and_then(|item| item.get("shutdown")).and_then(Value::as_str) == Some("clean")
}

/* The prefs.ui.set that stores shutdown clean sends true from the call that
   did the fsync. A page event sends the same answer when that command never
   ran, or false when the page could not quit. */
static QUIT_ACK: Mutex<Option<std::sync::mpsc::Sender<bool>>> = Mutex::new(None);
static QUIT_PAGE: AtomicU8 = AtomicU8::new(0);
/* The scavenger thread takes SIGINT with the default action, which kills the
   process. The handler only counts. sigwait still collects a signal that every
   thread has blocked. HANDLED is how many counts the watcher has accepted. */
static QUIT_HITS: AtomicU32 = AtomicU32::new(0);
static QUIT_HANDLED: AtomicU32 = AtomicU32::new(0);
const PAGE_BEGAN: u8 = 1;
const PAGE_STUCK: u8 = 2;
const PAGE_FAILED: u8 = 3;

fn note_quit_result(saved: bool) {
    if let Some(tx) = QUIT_ACK.lock().unwrap_or_else(|err| err.into_inner()).take() {
        let _ = tx.send(saved);
    }
}

fn note_page(stage: u8) {
    QUIT_PAGE.store(stage, Ordering::SeqCst);
}

#[tauri::command]
async fn core(cmd: String, args: Value) -> Result<Value, Value> {
    let clean = writes_clean_session(&cmd, &args);
    let result = tauri::async_runtime::spawn_blocking(move || core_data(&cmd, &args))
        .await
        .unwrap_or_else(|_| Err(fail("internal", "command task failed")));
    if clean {
        if result.is_err() {
            note_page(PAGE_FAILED);
        }
        note_quit_result(result.is_ok());
    }
    result
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
    let Ok(path) = CString::new(workdir) else { shell_error!("workdir contains a NUL"); std::process::exit(1); };
    let mut err = [0u8; 512];
    if unsafe { pavillion_start(path.as_ptr(), err.as_mut_ptr().cast(), err.len()) } != 0 {
        shell_error!("pavillion_start failed: {}", unsafe { CStr::from_ptr(err.as_ptr().cast()) }.to_string_lossy());
    }
    exit_if_profile_locked();
}

fn stop_events() {
    unsafe { pav_on_event(None, std::ptr::null_mut()) };
    EVENT_TX.lock().unwrap_or_else(|err| err.into_inner()).take();
}

fn quit_signals() -> libc::sigset_t {
    let mut set: libc::sigset_t = unsafe { std::mem::zeroed() };
    unsafe {
        libc::sigemptyset(&mut set);
        libc::sigaddset(&mut set, libc::SIGINT);
        libc::sigaddset(&mut set, libc::SIGTERM);
    }
    set
}

/* GLib starts WebKit's page process with SIGINT reset to terminate, and it
   does not clear a mask this process has blocked. Block both signals on this
   thread before any child exists so the page inherits the block and stays up
   to run the save. bmalloc's scavenger, inside this process, waits with the
   mask cleared. A default SIGINT there kills the app before sigwait runs. */
pub fn block_quit_signals() {
    let set = quit_signals();
    if unsafe { libc::pthread_sigmask(libc::SIG_BLOCK, &set, std::ptr::null_mut()) } != 0 {
        shell_warn!("cannot block SIGINT");
        std::process::exit(1);
    }
}

unsafe extern "C" fn on_quit_signal(_sig: i32) {
    QUIT_HITS.fetch_add(1, Ordering::SeqCst);
}

fn install_quit_handler() {
    unsafe {
        let mut action: libc::sigaction = std::mem::zeroed();
        action.sa_sigaction = on_quit_signal as *const () as libc::sighandler_t;
        action.sa_flags = libc::SA_RESTART;
        libc::sigemptyset(&mut action.sa_mask);
        if libc::sigaction(libc::SIGINT, &action, std::ptr::null_mut()) != 0
            || libc::sigaction(libc::SIGTERM, &action, std::ptr::null_mut()) != 0
        {
            shell_warn!("cannot catch SIGINT");
            std::process::exit(1);
        }
    }
}

/* WebKitGTK 2.52 aborts its web process during EGL teardown after a normal
   quit, which prints `free(): corrupted unsorted chunks` on this terminal.
   The profile write has already finished. No accelerated compositing means
   no GL context racing Mesa's atexit handler. A value already in the
   environment is left alone. */
fn quiet_webkit_exit() {
    if std::env::var_os("WEBKIT_DISABLE_COMPOSITING_MODE").is_some() {
        return;
    }
    unsafe {
        std::env::set_var("WEBKIT_DISABLE_COMPOSITING_MODE", "1");
    }
}

fn quit_timeout_message() -> &'static str {
    match QUIT_PAGE.load(Ordering::SeqCst) {
        PAGE_STUCK => "session save had no quit handler",
        PAGE_FAILED => "session save failed",
        PAGE_BEGAN => "session save did not finish",
        _ => "session save did not run",
    }
}

fn wait_quit_signal() -> bool {
    let set = quit_signals();
    loop {
        let hits = QUIT_HITS.load(Ordering::SeqCst);
        if hits != QUIT_HANDLED.load(Ordering::SeqCst) {
            QUIT_HANDLED.store(hits, Ordering::SeqCst);
            return true;
        }
        let mut info: libc::siginfo_t = unsafe { std::mem::zeroed() };
        let timeout = libc::timespec { tv_sec: 0, tv_nsec: 50_000_000 };
        let rc = unsafe { libc::sigtimedwait(&set, &mut info, &timeout) };
        if rc > 0 {
            return true;
        }
        let err = std::io::Error::last_os_error().raw_os_error();
        if err != Some(libc::EAGAIN) && err != Some(libc::EINTR) {
            return false;
        }
    }
}

/* Ctrl+C is not a window close. Ask the page for the same quit the title bar
   uses. The wait ends when that prefs.ui.set returns, not when the window
   disappears. Another signal during the wait does not cut the write off. */
fn watch_sigint(app: tauri::AppHandle) {
    std::thread::spawn(move || {
        if !wait_quit_signal() {
            shell_warn!("cannot catch SIGINT");
            return;
        }
        let (tx, rx) = std::sync::mpsc::channel();
        QUIT_PAGE.store(0, Ordering::SeqCst);
        *QUIT_ACK.lock().unwrap_or_else(|err| err.into_inner()) = Some(tx);
        let started = app.get_webview_window("main").and_then(|window| {
            window.eval("window.__pavQuit&&window.__pavQuit()").ok()
        }).is_some();
        if !started {
            shell_warn!("session save did not start");
            let _ = app.exit(130);
            return;
        }
        let saved = match rx.recv_timeout(std::time::Duration::from_secs(2)) {
            Ok(saved) => saved,
            Err(_) => false,
        };
        if !saved {
            shell_warn!("{}", quit_timeout_message());
        }
        let _ = app.exit(if saved { 0 } else { 130 });
        if wait_quit_signal() {
            std::process::exit(if saved { 0 } else { 130 });
        }
    });
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
                shell_warn!("cannot emit pav: {err}");
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


fn canonical_workdir(workdir: &str) -> String {
    let path = std::fs::canonicalize(workdir).map(|p| p.to_string_lossy().into_owned()).unwrap_or_else(|_| workdir.to_string());
    let trimmed = path.trim_end_matches('/');
    if trimmed.is_empty() { "/".to_string() } else { trimmed.to_string() }
}

/// Single-instance id (O-COMPOSE Lock): the default workdir keeps the app id;
/// any other workdir appends the first 32 hex of SHA-256 of its canonical path (V Lock hash).
fn instance_identifier(base: &str, workdir: &str) -> String {
    use sha2::{Digest, Sha256};
    let canonical = canonical_workdir(workdir);
    let default = std::env::var("HOME").ok().filter(|h| !h.is_empty()).map(|h| canonical_workdir(&format!("{h}/.local/share/pavillion")));
    if default.as_deref() == Some(canonical.as_str()) {
        return base.to_string();
    }
    let digest = Sha256::digest(canonical.as_bytes());
    let hex: String = digest.iter().take(16).map(|b| format!("{b:02x}")).collect();
    format!("{base}.w{hex}")
}

fn focus_existing(app: &tauri::AppHandle) {
    core_log(2, "second launch of this workdir: focusing the existing window");
    let Some(window) = app.get_webview_window("main") else { shell_warn!("second launch: main window is missing"); return };
    if let Err(e) = window.unminimize() { shell_warn!("second launch: cannot unminimize the window ({e})"); }
    if let Err(e) = window.show() { shell_warn!("second launch: cannot show the window ({e})"); }
    if let Err(e) = window.set_focus() { shell_warn!("second launch: cannot focus the window ({e})"); }
}

/// O-COMPOSE Lock (2026-10-19): the one line a refused second launch prints.
const SECOND_LAUNCH_MSG: &str = "Pavillion is already running for this profile; bringing it to the front.";
const PROFILE_LOCKED_MSG: &str = "This profile is open in another Pavillion window.";
/// True while the app is being built: the single-instance plugin exits the
/// second process from inside build, before our setup runs.
static IN_BUILD: AtomicBool = AtomicBool::new(false);

fn say_second_launch() {
    eprintln!("{SECOND_LAUNCH_MSG}");
}

extern "C" fn exit_during_build() {
    if IN_BUILD.load(Ordering::SeqCst) {
        say_second_launch();
    }
}

/// The profile lock refused this process: say so, log it, and leave quietly.
fn exit_if_profile_locked() {
    let locked = core_data("system.status", &json!({}))
        .ok()
        .and_then(|v| v.get("error").and_then(Value::as_str).map(|e| e == PROFILE_LOCKED_MSG))
        .unwrap_or(false);
    if locked {
        say_second_launch();
        core_log(2, SECOND_LAUNCH_MSG);
        unsafe { pavillion_stop() };
        std::process::exit(0);
    }
}


/// D-Bus well-known name for the single-instance plugin (G-LAYERS / O-COMPOSE Lock).
/// Each element: characters outside [A-Za-z0-9_-] become '_', and '_' is
/// prefixed when it starts with a digit (`500nodes` -> `_500nodes`). Empty
/// elements are dropped. The Tauri identifier itself is left alone.
pub(crate) fn dbus_name_for(identifier: &str) -> String {
    let elements: Vec<String> = identifier
        .split('.')
        .filter(|e| !e.is_empty())
        .map(|e| {
            let mut s: String = e.chars().map(|c| if c.is_ascii_alphanumeric() || c == '_' || c == '-' { c } else { '_' }).collect();
            if s.starts_with(|c: char| c.is_ascii_digit()) {
                s.insert(0, '_');
            }
            s
        })
        .collect();
    if elements.len() < 2 { format!("pavillion.{}", elements.join(".")).trim_end_matches('.').to_string() } else { elements.join(".") }
}

/// The plugin appends ".SingleInstance"; D-Bus caps a name at 255 bytes.
pub(crate) fn dbus_name_valid(name: &str) -> bool {
    let full = format!("{name}.SingleInstance");
    full.len() <= 255
        && full.split('.').count() >= 2
        && full.split('.').all(|e| {
            !e.is_empty()
                && !e.starts_with(|c: char| c.is_ascii_digit())
                && e.chars().all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
        })
}

fn build_app(workdir: &str, single_instance: bool) -> Result<tauri::App, tauri::Error> {
    let mut context = tauri::generate_context!();
    let identifier = instance_identifier(&context.config().identifier, workdir);
    let dbus_id = dbus_name_for(&identifier);
    context.config_mut().identifier = identifier;
    let workdir = workdir.to_string();
    let mut builder = tauri::Builder::default();
    if single_instance {
        if dbus_name_valid(&dbus_id) {
            builder = builder.plugin(
                tauri_plugin_single_instance::Builder::new()
                    .dbus_id(dbus_id)
                    .callback(|app, _argv, _cwd| focus_existing(app))
                    .build(),
            );
        } else {
            shell_warn!("second-window check is off: the instance name is not usable here; the profile lock still stops a second window from opening this profile");
        }
    }
    let mut builder = builder.plugin(tauri_plugin_dialog::init()).invoke_handler(tauri::generate_handler![core]);
    for scheme in ["pav-blob", "pav-cid", "pav-link", "pav-theme"] {
        let name = scheme.to_string();
        builder = builder.register_uri_scheme_protocol(scheme, move |_ctx, request| scheme_reply(&name, &request));
    }
    builder.on_page_load(|webview, payload| {
        if payload.event() != tauri::webview::PageLoadEvent::Finished || webview.label() != "main" { return; }
        let url = payload.url().as_str();
        if url.starts_with("about:") { return; }
        let Some(window) = webview.get_webview_window("main") else { return };
        note_splash_painted(&window);
    }).setup(move |app| {
        IN_BUILD.store(false, Ordering::SeqCst);
        start_core(&workdir);
        start_events(app.handle());
        let Some(window) = app.get_webview_window("main") else { shell_error!("main window is missing"); return Ok(()) };
        let watch = window.clone();
        window.on_window_event(move |event| {
            if WINDOW_READY.load(Ordering::SeqCst) && matches!(event, tauri::WindowEvent::Resized(_) | tauri::WindowEvent::Moved(_)) {
                report_window(&watch);
            }
        });
        let painted = window.clone();
        app.listen("pav-splash-painted", move |_| note_splash_painted(&painted));
        arm_reveal_timeout(window.clone());
        apply_initial(&window);
        app.listen("pav-quit-began", |_| note_page(PAGE_BEGAN));
        app.listen("pav-quit-stuck", |_| {
            note_page(PAGE_STUCK);
            note_quit_result(false);
        });
        app.listen("pav-quit-failed", |_| {
            note_page(PAGE_FAILED);
            note_quit_result(false);
        });
        app.listen("pav-quit-done", |_| note_quit_result(true));
        Ok(())
    }).build(context)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run(workdir: &str) {
    block_quit_signals();
    install_quit_handler();
    quiet_webkit_exit();
    unsafe { libc::atexit(exit_during_build) };
    IN_BUILD.store(true, Ordering::SeqCst);
    let app = match build_app(workdir, true) {
        Ok(app) => app,
        Err(tauri::Error::PluginInitialization(name, reason)) if name == "single-instance" => {
            shell_warn!("could not set up the check for a second window ({reason}); continuing without it. The profile lock still stops a second window from opening this profile");
            IN_BUILD.store(false, Ordering::SeqCst);
            build_app(workdir, false).expect("error while building pavillion")
        }
        Err(e) => {
            IN_BUILD.store(false, Ordering::SeqCst);
            panic!("error while building pavillion: {e}")
        }
    };
    IN_BUILD.store(false, Ordering::SeqCst);
    watch_sigint(app.handle().clone());
    app.run(|app, event| match event {
        tauri::RunEvent::ExitRequested { .. } => {
            WINDOW_READY.store(false, Ordering::SeqCst);
            if let Some(window) = app.get_webview_window("main") { report_window(&window); }
        }
        tauri::RunEvent::Exit => { stop_core_and_events(&app); }
        _ => {}
    });
}

#[cfg(debug_assertions)]
fn drain_webkit_before_exit(app: &tauri::AppHandle) {
    if let Some(window) = app.get_webview_window("main") {
        let webview = window.as_ref();
        let (tx, rx) = std::sync::mpsc::channel::<()>();
        let _ = webview.eval_with_callback(
            "if(window.__pavDrainWebkit){Promise.resolve(window.__pavDrainWebkit()).then(()=>void 0)}",
            move |_result| { let _ = tx.send(()); },
        );
        let _ = rx.recv_timeout(std::time::Duration::from_millis(250));
        let _ = webview.close_devtools();
        std::thread::sleep(std::time::Duration::from_millis(150));
    }
}

#[cfg(not(debug_assertions))]
fn drain_webkit_before_exit(_app: &tauri::AppHandle) {}

fn stop_core_and_events(app: &tauri::AppHandle) {
    stop_events();
    drain_webkit_before_exit(app);
    unsafe { pavillion_stop() };
}

#[cfg(test)]
mod tests {
    use super::{dbus_name_for, dbus_name_valid};

    #[test]
    fn digit_led_element_gets_underscore() {
        assert_eq!(dbus_name_for("com.500nodes.pavillion"), "com._500nodes.pavillion");
        assert!(dbus_name_valid(&dbus_name_for("com.500nodes.pavillion")));
    }

    #[test]
    fn workdir_suffix_kept_and_bad_chars_replaced() {
        let id = "com.500nodes.pavillion.w0123456789abcdef0123456789abcdef";
        assert_eq!(dbus_name_for(id), "com._500nodes.pavillion.w0123456789abcdef0123456789abcdef");
        assert_eq!(dbus_name_for("com.my app.x+y"), "com.my_app.x_y");
        assert!(!dbus_name_valid("com.500nodes.pavillion"));
        assert!(!dbus_name_valid(&"a".repeat(300)));
    }
}
