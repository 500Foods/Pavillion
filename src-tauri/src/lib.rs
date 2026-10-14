use serde::Serialize;

#[repr(C)]
struct PavillionStatus {
    profile_name: [u8; 256],
    profile_id: [u8; 64],
    state: [u8; 32],
    error: [u8; 512],
}

unsafe extern "C" {
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

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![status])
        .run(tauri::generate_context!())
        .expect("error while running pavillion");
}
