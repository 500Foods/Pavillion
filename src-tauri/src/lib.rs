use std::ffi::{CStr, CString};
use std::fs;
use std::os::raw::c_char;
use std::os::unix::fs::PermissionsExt;
use std::path::Path;

use serde::Serialize;

#[repr(C)]
struct PavillionStatus {
    profile_name: [u8; 256],
    profile_id: [u8; 64],
    state: [u8; 32],
    error: [u8; 512],
}

unsafe extern "C" {
    fn pavillion_start(workdir: *const c_char, err: *mut c_char, err_len: usize) -> i32;
    fn pavillion_stop();
    fn pavillion_status(out: *mut PavillionStatus) -> i32;
}

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
        .invoke_handler(tauri::generate_handler![status])
        .build(tauri::generate_context!())
        .expect("error while building pavillion")
        .run(|_app, event| {
            if let tauri::RunEvent::Exit = event {
                unsafe { pavillion_stop() };
            }
        });
}
