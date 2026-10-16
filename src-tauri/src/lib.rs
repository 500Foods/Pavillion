use std::ffi::{CStr, CString, c_void};
use std::fs;
use std::os::raw::c_char;
use std::os::unix::fs::PermissionsExt;
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;
use std::time::Duration;

use serde::Serialize;
use serde_json::Value;
use tauri::{Emitter, LogicalPosition, LogicalSize, Manager, Position, Size, WebviewWindow};

#[derive(Debug, Serialize)]
struct ApiError {
    code: String,
    message: String,
    retryable: bool,
}

struct EventNote {
    name: String,
    json: String,
}

static EVENT_TX: Mutex<Option<tauri::async_runtime::Sender<EventNote>>> = Mutex::new(None);

unsafe extern "C" {
    fn pavillion_api_version() -> i32;
    fn pavillion_start(workdir: *const c_char, err: *mut c_char, err_len: usize) -> i32;
    fn pavillion_stop();
    fn pavillion_call(cmd: *const c_char, args_json: *const c_char, result_json: *mut *mut c_char) -> i32;
    fn pavillion_free(p: *mut c_void);
    fn pav_on_event(
        cb: Option<unsafe extern "C" fn(*const c_char, *const c_char, *mut c_void)>,
        user: *mut c_void,
    );
}

/// Copies the strings and queues them. The core frees `name` and `json` when
/// this returns, and this must not call `pavillion_call` or `pavillion_stop`.
unsafe extern "C" fn on_core_event(name: *const c_char, json: *const c_char, _user: *mut c_void) {
    if name.is_null() {
        return;
    }
    let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned();
    let json = if json.is_null() {
        "{}".to_string()
    } else {
        unsafe { CStr::from_ptr(json) }.to_string_lossy().into_owned()
    };
    let guard = EVENT_TX.lock().unwrap_or_else(|err| err.into_inner());
    if let Some(tx) = guard.as_ref() {
        let _ = tx.try_send(EventNote { name, json });
    }
}

fn api_error(code: &str, message: impl Into<String>) -> ApiError {
    ApiError {
        code: code.to_string(),
        message: message.into(),
        retryable: false,
    }
}

fn core_data(cmd: &str, args: &Value) -> Result<Value, ApiError> {
    let args_text = serde_json::to_string(args).map_err(|_| api_error("internal", "cannot encode arguments"))?;
    let c_cmd = CString::new(cmd).map_err(|_| api_error("bad_args", "command contains NUL"))?;
    let c_args = CString::new(args_text).map_err(|_| api_error("bad_args", "arguments contain NUL"))?;
    let mut result: *mut c_char = std::ptr::null_mut();
    let rc = unsafe { pavillion_call(c_cmd.as_ptr(), c_args.as_ptr(), &mut result) };
    if rc != 0 || result.is_null() {
        return Err(api_error("internal", "core produced no reply"));
    }
    let text = unsafe { CStr::from_ptr(result) }.to_string_lossy().into_owned();
    unsafe { pavillion_free(result.cast()) };
    let reply: Value = serde_json::from_str(&text).map_err(|_| api_error("internal", "reply is not JSON"))?;
    if reply.get("ok").and_then(Value::as_bool) == Some(true) {
        return Ok(reply.get("data").cloned().unwrap_or(Value::Null));
    }
    let err = reply.get("error");
    let code = err
        .and_then(|item| item.get("code"))
        .and_then(Value::as_str)
        .unwrap_or("internal");
    let message = err
        .and_then(|item| item.get("message"))
        .and_then(Value::as_str)
        .unwrap_or("core call failed");
    let retryable = err
        .and_then(|item| item.get("retryable"))
        .and_then(Value::as_bool)
        .unwrap_or(false);
    Err(ApiError {
        code: code.to_string(),
        message: message.to_string(),
        retryable,
    })
}

#[derive(Clone, Copy)]
struct RestoredWindow {
    width: i32,
    height: i32,
    x: Option<i32>,
    y: Option<i32>,
    known: bool,
}

struct WindowPrefs {
    maximized: bool,
    width: i32,
    height: i32,
    x: Option<i32>,
    y: Option<i32>,
}

static RESTORED: Mutex<RestoredWindow> = Mutex::new(RestoredWindow {
    width: 0,
    height: 0,
    x: None,
    y: None,
    known: false,
});
static WINDOW_GEN: AtomicU64 = AtomicU64::new(0);
static WINDOW_WAIT: AtomicBool = AtomicBool::new(false);
static WINDOW_READY: AtomicBool = AtomicBool::new(false);

fn int_field(data: &Value, key: &str, fallback: i32) -> i32 {
    data.get(key)
        .and_then(Value::as_i64)
        .and_then(|n| i32::try_from(n).ok())
        .unwrap_or(fallback)
}

fn opt_int_field(data: &Value, key: &str) -> Option<i32> {
    match data.get(key) {
        Some(value) if value.is_null() => None,
        Some(value) => value.as_i64().and_then(|n| i32::try_from(n).ok()),
        None => None,
    }
}

fn read_window() -> Result<WindowPrefs, String> {
    let data = core_data("prefs.get", &serde_json::json!({})).map_err(|err| err.message)?;
    Ok(WindowPrefs {
        maximized: data.get("windowMaximized").and_then(Value::as_bool).unwrap_or(false),
        width: int_field(&data, "windowWidth", 1200),
        height: int_field(&data, "windowHeight", 800),
        x: opt_int_field(&data, "windowX"),
        y: opt_int_field(&data, "windowY"),
    })
}

fn restored_lock() -> std::sync::MutexGuard<'static, RestoredWindow> {
    RESTORED.lock().unwrap_or_else(|err| err.into_inner())
}

fn logical_inner(window: &WebviewWindow) -> Option<(i32, i32)> {
    let scale = window.scale_factor().ok()?;
    if scale <= 0.0 {
        return None;
    }
    let size = window.inner_size().ok()?;
    let width = (size.width as f64 / scale).round();
    let height = (size.height as f64 / scale).round();
    if !(200.0..=10000.0).contains(&width) || !(200.0..=10000.0).contains(&height) {
        return None;
    }
    Some((width as i32, height as i32))
}

fn logical_outer_pos(window: &WebviewWindow) -> Option<(i32, i32)> {
    let scale = window.scale_factor().ok()?;
    if scale <= 0.0 {
        return None;
    }
    let pos = window.outer_position().ok()?;
    let x = (pos.x as f64 / scale).round();
    let y = (pos.y as f64 / scale).round();
    if !(-32000.0..=32000.0).contains(&x) || !(-32000.0..=32000.0).contains(&y) {
        return None;
    }
    Some((x as i32, y as i32))
}

fn note_window(window: &WebviewWindow) {
    if window.is_maximized().unwrap_or(false) {
        return;
    }
    let Some((width, height)) = logical_inner(window) else {
        return;
    };
    let mut restored = restored_lock();
    restored.width = width;
    restored.height = height;
    restored.known = true;
    if let Some((x, y)) = logical_outer_pos(window) {
        restored.x = Some(x);
        restored.y = Some(y);
    }
}

fn seed_restored(prefs: &WindowPrefs) {
    let mut restored = restored_lock();
    restored.width = prefs.width;
    restored.height = prefs.height;
    restored.x = prefs.x;
    restored.y = prefs.y;
    restored.known = true;
}

fn save_window_now(window: &WebviewWindow) {
    let restored = *restored_lock();
    if !restored.known {
        return;
    }
    let args = serde_json::json!({
        "windowMaximized": window.is_maximized().unwrap_or(false),
        "windowWidth": restored.width,
        "windowHeight": restored.height,
        "windowX": restored.x,
        "windowY": restored.y,
    });
    if let Err(err) = core_data("prefs.set", &args) {
        eprintln!("cannot save window: {}", err.message);
    }
}

fn schedule_window_save(window: WebviewWindow) {
    if !WINDOW_READY.load(Ordering::SeqCst) {
        return;
    }
    WINDOW_GEN.fetch_add(1, Ordering::SeqCst);
    if WINDOW_WAIT.swap(true, Ordering::SeqCst) {
        return;
    }
    std::thread::spawn(move || {
        loop {
            let stamp = WINDOW_GEN.load(Ordering::SeqCst);
            std::thread::sleep(Duration::from_millis(400));
            if !WINDOW_READY.load(Ordering::SeqCst) {
                WINDOW_WAIT.store(false, Ordering::SeqCst);
                break;
            }
            if WINDOW_GEN.load(Ordering::SeqCst) != stamp {
                continue;
            }
            save_window_now(&window);
            WINDOW_WAIT.store(false, Ordering::SeqCst);
            if WINDOW_GEN.load(Ordering::SeqCst) == stamp {
                break;
            }
            if WINDOW_WAIT.swap(true, Ordering::SeqCst) {
                break;
            }
        }
    });
}

fn on_window_change(window: &WebviewWindow) {
    if !WINDOW_READY.load(Ordering::SeqCst) {
        return;
    }
    note_window(window);
    schedule_window_save(window.clone());
}

fn apply_saved_window(window: &WebviewWindow) {
    let prefs = match read_window() {
        Ok(prefs) => prefs,
        Err(err) => {
            eprintln!("cannot read preferences: {err}");
            let _ = window.show();
            return;
        }
    };
    seed_restored(&prefs);
    if prefs.width >= 200 && prefs.height >= 200 {
        let size = LogicalSize::new(prefs.width as f64, prefs.height as f64);
        if let Err(err) = window.set_size(Size::Logical(size)) {
            eprintln!("cannot set window size: {err}");
        }
    }
    if let (Some(x), Some(y)) = (prefs.x, prefs.y) {
        let pos = LogicalPosition::new(x as f64, y as f64);
        if let Err(err) = window.set_position(Position::Logical(pos)) {
            eprintln!("cannot set window position: {err}");
        }
    }
    if prefs.maximized {
        if let Err(err) = window.maximize() {
            eprintln!("cannot maximize window: {err}");
        }
    }
    if let Err(err) = window.show() {
        eprintln!("cannot show window: {err}");
    }
    WINDOW_READY.store(true, Ordering::SeqCst);
}

#[tauri::command]
async fn core(cmd: String, args: Value) -> Result<Value, ApiError> {
    match tauri::async_runtime::spawn_blocking(move || core_data(&cmd, &args)).await {
        Ok(result) => result,
        Err(_) => Err(api_error("internal", "command task failed")),
    }
}

fn ensure_workdir(workdir: &str) {
    let path = Path::new(workdir);
    if path.exists() {
        return;
    }
    if let Err(err) = fs::create_dir_all(path) {
        eprintln!("cannot create {}: {err}", path.display());
        return;
    }
    let mode = fs::Permissions::from_mode(0o700);
    if let Err(err) = fs::set_permissions(path, mode) {
        eprintln!("cannot set mode on {}: {err}", path.display());
    }
}

fn require_api_version() {
    let core = unsafe { pavillion_api_version() };
    let shell: i32 = env!("PAVILLION_API_VERSION")
        .parse()
        .expect("PAVILLION_API_VERSION from build.rs");
    if core != shell {
        eprintln!("API version mismatch: core={core} shell={shell}");
        std::process::exit(1);
    }
}

fn start_core(workdir: &str) {
    require_api_version();
    let path = match CString::new(workdir) {
        Ok(path) => path,
        Err(_) => {
            eprintln!("workdir contains a NUL");
            std::process::exit(1);
        }
    };
    ensure_workdir(workdir);
    let mut err = [0u8; 512];
    let rc = unsafe { pavillion_start(path.as_ptr(), err.as_mut_ptr() as *mut c_char, err.len()) };
    if rc != 0 {
        let message = unsafe { CStr::from_ptr(err.as_ptr() as *const c_char) };
        eprintln!("pavillion_start failed: {}", message.to_string_lossy());
    }
}

fn stop_events() {
    unsafe { pav_on_event(None, std::ptr::null_mut()) };
    let mut slot = EVENT_TX.lock().unwrap_or_else(|err| err.into_inner());
    slot.take();
}

fn start_events(app: &tauri::AppHandle) {
    let (tx, mut rx) = tauri::async_runtime::channel::<EventNote>(64);
    {
        let mut slot = EVENT_TX.lock().unwrap_or_else(|err| err.into_inner());
        *slot = Some(tx);
    }
    unsafe { pav_on_event(Some(on_core_event), std::ptr::null_mut()) };
    let handle = app.clone();
    tauri::async_runtime::spawn(async move {
        while let Some(note) = rx.recv().await {
            let payload = serde_json::from_str::<Value>(&note.json).unwrap_or(Value::String(note.json));
            let body = serde_json::json!({ "name": note.name, "payload": payload });
            if let Err(err) = handle.emit("pav", body) {
                eprintln!("cannot emit pav: {err}");
            }
        }
    });
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run(workdir: &str) {
    start_core(workdir);
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![core])
        .setup(|app| {
            start_events(app.handle());
            let Some(window) = app.get_webview_window("main") else {
                eprintln!("main window is missing");
                return Ok(());
            };
            let watch = window.clone();
            window.on_window_event(move |event| {
                if matches!(
                    event,
                    tauri::WindowEvent::Resized(_) | tauri::WindowEvent::Moved(_)
                ) {
                    on_window_change(&watch);
                }
            });
            apply_saved_window(&window);
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building pavillion")
        .run(|app, event| match event {
            tauri::RunEvent::ExitRequested { .. } => {
                WINDOW_READY.store(false, Ordering::SeqCst);
                if let Some(window) = app.get_webview_window("main") {
                    save_window_now(&window);
                }
            }
            tauri::RunEvent::Exit => {
                stop_events();
                unsafe { pavillion_stop() };
            }
            _ => {}
        });
}
