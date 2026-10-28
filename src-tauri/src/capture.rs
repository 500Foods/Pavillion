use std::ffi::CString;

unsafe extern "C" {
    fn pav_ui_log(subsystem: *const std::os::raw::c_char, level: std::os::raw::c_int, message: *const std::os::raw::c_char) -> std::os::raw::c_int;
    fn pav_log_suppress_stderr(on: std::os::raw::c_int) -> std::os::raw::c_int;
}

#[cfg(not(unix))]
pub fn install_native_stderr_capture() {}

#[cfg(unix)]
pub fn install_native_stderr_capture() {
    if !cfg!(target_os = "linux") {
        return;
    }

    unsafe {
        let mut pipe_fds = [0i32; 2];
        if libc::pipe2(pipe_fds.as_mut_ptr(), libc::O_CLOEXEC) != 0 {
            return;
        }
        let read_fd = pipe_fds[0];
        let write_fd = pipe_fds[1];

        libc::dup2(write_fd, 2);
        libc::close(write_fd);

        let read_fd_owned = read_fd;
        std::thread::Builder::new()
            .name("stderr-capture".to_string())
            .spawn(move || {
                capture_reader(read_fd_owned);
            })
            .ok();
    }
}

#[cfg(unix)]
fn capture_reader(read_fd: i32) {
    let mut buf = [0u8; 1024];
    let mut line: Vec<u8> = Vec::new();

    loop {
        let n = unsafe { libc::read(read_fd, buf.as_mut_ptr() as *mut libc::c_void, buf.len()) };
        if n <= 0 {
            break;
        }

        for &byte in &buf[..n as usize] {
            if byte == b'\n' {
                if !line.is_empty() {
                    handle_line(&line);
                    line.clear();
                }
            } else {
                line.push(byte);
            }
        }
    }

    if !line.is_empty() {
        handle_line(&line);
    }
}

#[cfg_attr(not(unix), allow(dead_code))]
fn handle_line(line: &[u8]) {
    if let Ok(text) = std::str::from_utf8(line) {
        if let Some((level, subsystem, message)) = parse_pav_line(text) {
            forward_log(level, &subsystem, &message);
        } else if text.contains("received NeedDebuggerBreak trap") {
            forward_log(1, "webkit", text);
        } else {
            forward_log(2, "native_stderr", text);
        }
    }
}

#[cfg_attr(not(unix), allow(dead_code))]
fn parse_pav_line(line: &str) -> Option<(i32, String, String)> {
    let bytes = line.as_bytes();
    if bytes.len() < 33 || bytes[24] != b' ' || bytes[30] != b' ' {
        return None;
    }
    let level_str = line.get(25..30).unwrap_or("").trim();
    let level = level_from_name(level_str)?;
    let rest = line.get(31..)?;
    let gap = rest.find("  ")?;
    if gap == 0 {
        return None;
    }
    let subsystem = rest[..gap].to_string();
    let message = rest[gap + 2..].to_string();
    Some((level, subsystem, message))
}

#[cfg_attr(not(unix), allow(dead_code))]
fn level_from_name(name: &str) -> Option<i32> {
    match name {
        "ERROR" => Some(0),
        "WARN"  => Some(1),
        "INFO"  => Some(2),
        "DEBUG" => Some(3),
        "TRACE" => Some(4),
        _ => None,
    }
}

#[cfg_attr(not(unix), allow(dead_code))]
fn forward_log(level: i32, subsystem: &str, message: &str) {
    let subsystem_c = CString::new(subsystem).unwrap_or_default();
    let message_c = CString::new(message).unwrap_or_default();
    unsafe {
        pav_log_suppress_stderr(1);
        pav_ui_log(subsystem_c.as_ptr(), level, message_c.as_ptr());
        pav_log_suppress_stderr(0);
    }
}
