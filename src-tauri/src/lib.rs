use std::ffi::{CStr, CString};
use std::fs;
use std::os::raw::c_char;
use std::os::unix::fs::PermissionsExt;
use std::path::Path;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Mutex;
use std::time::Duration;

use serde::{Deserialize, Serialize};
use tauri::{LogicalPosition, LogicalSize, Manager, Position, Size, WebviewWindow};

const PAV_PREFS_ZOOM: u32 = 1;
const PAV_PREFS_WINDOW: u32 = 2;
const PAV_PREFS_UI: u32 = 4;

#[repr(C)]
struct PavillionStatus {
    profile_name: [u8; 256],
    profile_id: [u8; 64],
    state: [u8; 32],
    error: [u8; 512],
}

#[repr(C)]
struct PavShellPrefs {
    zoom_shell: f64,
    mail_split: f64,
    mail_min: f64,
    mail_max: f64,
    contacts_split: f64,
    contacts_min: f64,
    contacts_max: f64,
    files_split: f64,
    files_min: f64,
    files_max: f64,
    window_maximized: i32,
    window_width: i32,
    window_height: i32,
    window_x_set: i32,
    window_y_set: i32,
    window_x: i32,
    window_y: i32,
    nav_width: i32,
    nav_min: i32,
    nav_max: i32,
    chrome: [u8; 16],
}

unsafe extern "C" {
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
        zoom_shell: 1.0,
        mail_split: 0.5,
        mail_min: 0.25,
        mail_max: 0.75,
        contacts_split: 0.5,
        contacts_min: 0.25,
        contacts_max: 0.75,
        files_split: 0.5,
        files_min: 0.25,
        files_max: 0.75,
        window_maximized: 0,
        window_width: 1200,
        window_height: 800,
        window_x_set: 0,
        window_y_set: 0,
        window_x: 0,
        window_y: 0,
        nav_width: 320,
        nav_min: 200,
        nav_max: 500,
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

#[derive(Serialize)]
struct PanePx {
    width: i32,
    min: i32,
    max: i32,
}

#[derive(Serialize)]
struct PaneSplit {
    split: f64,
    min: f64,
    max: f64,
}

#[derive(Serialize)]
struct UiReply {
    nav: PanePx,
    mail: PaneSplit,
    contacts: PaneSplit,
    files: PaneSplit,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct WindowReply {
    maximized: bool,
    width: i32,
    height: i32,
    x: Option<i32>,
    y: Option<i32>,
    chrome: String,
}

#[derive(Serialize)]
struct PrefsReply {
    zoom: f64,
    window: WindowReply,
    ui: UiReply,
}

#[tauri::command]
fn prefs() -> Result<PrefsReply, String> {
    let prefs = read_prefs()?;
    Ok(PrefsReply {
        zoom: prefs.zoom_shell,
        window: WindowReply {
            maximized: prefs.window_maximized != 0,
            width: prefs.window_width,
            height: prefs.window_height,
            x: (prefs.window_x_set != 0).then_some(prefs.window_x),
            y: (prefs.window_y_set != 0).then_some(prefs.window_y),
            chrome: c_field(&prefs.chrome),
        },
        ui: UiReply {
            nav: PanePx {
                width: prefs.nav_width,
                min: prefs.nav_min,
                max: prefs.nav_max,
            },
            mail: PaneSplit {
                split: prefs.mail_split,
                min: prefs.mail_min,
                max: prefs.mail_max,
            },
            contacts: PaneSplit {
                split: prefs.contacts_split,
                min: prefs.contacts_min,
                max: prefs.contacts_max,
            },
            files: PaneSplit {
                split: prefs.files_split,
                min: prefs.files_min,
                max: prefs.files_max,
            },
        },
    })
}

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
struct LayoutSave {
    zoom: f64,
    nav_width: i32,
    mail_split: f64,
    contacts_split: f64,
    files_split: f64,
}

#[tauri::command]
fn save_layout(layout: LayoutSave) -> Result<(), String> {
    let mut prefs = read_prefs()?;
    prefs.zoom_shell = layout.zoom;
    prefs.nav_width = layout.nav_width;
    prefs.mail_split = layout.mail_split;
    prefs.contacts_split = layout.contacts_split;
    prefs.files_split = layout.files_split;
    write_prefs(&prefs, PAV_PREFS_ZOOM | PAV_PREFS_UI)
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

fn start_core(workdir: &str) {
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
        .invoke_handler(tauri::generate_handler![status, prefs, save_layout])
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
