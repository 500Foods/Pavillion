use std::ffi::{CStr, CString};
use std::fs;
use std::os::raw::c_char;
use std::os::unix::fs::PermissionsExt;
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;
use std::time::Duration;

use serde::Serialize;
use tauri::{LogicalPosition, LogicalSize, Manager, Position, Size, WebviewWindow};

const PAV_PREFS_WINDOW: u32 = 2;

#[repr(C)]
struct PavillionStatus {
    profile_name: [u8; 256],
    profile_id: [u8; 64],
    state: [u8; 32],
    error: [u8; 512],
}

#[repr(C)]
struct PavShellPrefs {
    window_maximized: i32,
    window_width: i32,
    window_height: i32,
    window_x_set: i32,
    window_y_set: i32,
    window_x: i32,
    window_y: i32,
    chrome: [u8; 16],
}

unsafe extern "C" {
    fn pavillion_api_version() -> i32;
    fn pavillion_start(workdir: *const c_char, err: *mut c_char, err_len: usize) -> i32;
    fn pavillion_stop();
    fn pavillion_status(out: *mut PavillionStatus) -> i32;
    fn pavillion_prefs_get(out: *mut PavShellPrefs) -> i32;
    fn pavillion_prefs_apply(
        prefs: *const PavShellPrefs,
        mask: u32,
        err: *mut c_char,
        err_len: usize,
    ) -> i32;
    fn pav_ui_log(subsystem: *const c_char, level: i32, message: *const c_char) -> i32;
    fn pavillion_ui_get(json_out: *mut *mut c_char, err: *mut c_char, err_len: usize) -> i32;
    fn pavillion_ui_set(json: *const c_char, err: *mut c_char, err_len: usize) -> i32;
}

#[derive(Clone, Copy)]
struct RestoredWindow {
    width: i32,
    height: i32,
    x_set: i32,
    y_set: i32,
    x: i32,
    y: i32,
    known: bool,
}

static RESTORED: Mutex<RestoredWindow> = Mutex::new(RestoredWindow {
    width: 0,
    height: 0,
    x_set: 0,
    y_set: 0,
    x: 0,
    y: 0,
    known: false,
});
static WINDOW_GEN: AtomicU64 = AtomicU64::new(0);
static WINDOW_WAIT: AtomicBool = AtomicBool::new(false);
static WINDOW_READY: AtomicBool = AtomicBool::new(false);

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct StatusReply {
    profile_name: String,
    profile_id: String,
    state: String,
    error: String,
}

fn c_field(bytes: &[u8]) -> String {
    let end = bytes.iter().position(|b| *b == 0).unwrap_or(bytes.len());
    String::from_utf8_lossy(&bytes[..end]).into_owned()
}

fn c_error(bytes: &[u8], rc: i32) -> String {
    let message = c_field(bytes);
    if message.is_empty() {
        format!("core returned {rc}")
    } else {
        message
    }
}

fn read_prefs() -> Result<PavShellPrefs, String> {
    let mut out = PavShellPrefs {
        window_maximized: 0,
        window_width: 1200,
        window_height: 800,
        window_x_set: 0,
        window_y_set: 0,
        window_x: 0,
        window_y: 0,
        chrome: [0; 16],
    };
    let rc = unsafe { pavillion_prefs_get(&mut out) };
    if rc != 0 {
        return Err(format!("pavillion_prefs_get returned {rc}"));
    }
    Ok(out)
}

fn write_prefs(prefs: &PavShellPrefs, mask: u32) -> Result<(), String> {
    let mut err = [0u8; 512];
    let rc = unsafe {
        pavillion_prefs_apply(
            prefs,
            mask,
            err.as_mut_ptr() as *mut c_char,
            err.len(),
        )
    };
    if rc != 0 {
        return Err(c_error(&err, rc));
    }
    Ok(())
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
        restored.x = x;
        restored.y = y;
        restored.x_set = 1;
        restored.y_set = 1;
    }
}

fn seed_restored(prefs: &PavShellPrefs) {
    let mut restored = restored_lock();
    restored.width = prefs.window_width;
    restored.height = prefs.window_height;
    restored.x = prefs.window_x;
    restored.y = prefs.window_y;
    restored.x_set = prefs.window_x_set;
    restored.y_set = prefs.window_y_set;
    restored.known = true;
}

fn save_window_now(window: &WebviewWindow) {
    let restored = *restored_lock();
    if !restored.known {
        return;
    }
    let mut prefs = match read_prefs() {
        Ok(prefs) => prefs,
        Err(err) => {
            eprintln!("cannot read preferences: {err}");
            return;
        }
    };
    prefs.window_maximized = i32::from(window.is_maximized().unwrap_or(false));
    prefs.window_width = restored.width;
    prefs.window_height = restored.height;
    prefs.window_x = restored.x;
    prefs.window_y = restored.y;
    prefs.window_x_set = restored.x_set;
    prefs.window_y_set = restored.y_set;
    if let Err(err) = write_prefs(&prefs, PAV_PREFS_WINDOW) {
        eprintln!("cannot save window: {err}");
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
    let prefs = match read_prefs() {
        Ok(prefs) => prefs,
        Err(err) => {
            eprintln!("cannot read preferences: {err}");
            let _ = window.show();
            return;
        }
    };
    seed_restored(&prefs);
    if prefs.window_width >= 200 && prefs.window_height >= 200 {
        let size = LogicalSize::new(prefs.window_width as f64, prefs.window_height as f64);
        if let Err(err) = window.set_size(Size::Logical(size)) {
            eprintln!("cannot set window size: {err}");
        }
    }
    if prefs.window_x_set != 0 && prefs.window_y_set != 0 {
        let pos = LogicalPosition::new(prefs.window_x as f64, prefs.window_y as f64);
        if let Err(err) = window.set_position(Position::Logical(pos)) {
            eprintln!("cannot set window position: {err}");
        }
    }
    if prefs.window_maximized != 0 {
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
fn status() -> Result<StatusReply, String> {
    let mut out = PavillionStatus {
        profile_name: [0; 256],
        profile_id: [0; 64],
        state: [0; 32],
        error: [0; 512],
    };
    let rc = unsafe { pavillion_status(&mut out) };
    if rc != 0 {
        return Err(format!("pavillion_status returned {rc}"));
    }
    Ok(StatusReply {
        profile_name: c_field(&out.profile_name),
        profile_id: c_field(&out.profile_id),
        state: c_field(&out.state),
        error: c_field(&out.error),
    })
}

#[tauri::command]
fn ui_get() -> Result<String, String> {
    let mut err = [0u8; 512];
    let mut json_ptr: *mut c_char = std::ptr::null_mut();
    let rc = unsafe {
        pavillion_ui_get(
            &mut json_ptr,
            err.as_mut_ptr() as *mut c_char,
            err.len(),
        )
    };
    if rc != 0 || json_ptr.is_null() {
        return Err(c_error(&err, rc));
    }
    let owned = unsafe { CString::from_raw(json_ptr) };
    Ok(owned.to_string_lossy().into_owned())
}

#[tauri::command]
fn ui_set(json: String) -> Result<(), String> {
    let c_json = CString::new(json).map_err(|_| "ui json contains NUL".to_string())?;
    let mut err = [0u8; 512];
    let rc = unsafe {
        pavillion_ui_set(
            c_json.as_ptr(),
            err.as_mut_ptr() as *mut c_char,
            err.len(),
        )
    };
    if rc != 0 {
        return Err(c_error(&err, rc));
    }
    Ok(())
}

#[tauri::command]
fn ui_log(subsystem: String, level: i32, message: String) -> Result<(), String> {
    let c_subsystem = match CString::new(subsystem.clone()) {
        Ok(s) => s,
        Err(_) => return Err("subsystem contains NUL".into()),
    };
    let c_message = match CString::new(message.clone()) {
        Ok(s) => s,
        Err(_) => return Err("message contains NUL".into()),
    };
    let rc = unsafe {
        pav_ui_log(
            c_subsystem.as_ptr(),
            level,
            c_message.as_ptr(),
        )
    };
    if rc != 0 {
        return Err(format!("pav_ui_log returned {rc}"));
    }
    Ok(())
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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run(workdir: &str) {
    start_core(workdir);
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![status, ui_get, ui_set, ui_log])
        .setup(|app| {
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
            tauri::RunEvent::Exit => unsafe { pavillion_stop() },
            _ => {}
        });
}
