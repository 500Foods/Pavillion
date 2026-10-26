use std::ffi::CString;

unsafe extern "C" {
    fn pav_ui_log(subsystem: *const std::os::raw::c_char, level: std::os::raw::c_int, message: *const std::os::raw::c_char) -> std::os::raw::c_int;
    fn pav_log_suppress_stderr(on: std::os::raw::c_int) -> std::os::raw::c_int;
}

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

fn handle_line(line: &[u8]) {
    if let Ok(text) = std::str::from_utf8(line) {
        if text.contains("received NeedDebuggerBreak trap") {
            forward_log(1, "webkit", text);
        } else {
            forward_log(2, "native_stderr", text);
        }
    }
}

fn forward_log(level: i32, subsystem: &str, message: &str) {
    let subsystem_c = CString::new(subsystem).unwrap_or_default();
    let message_c = CString::new(message).unwrap_or_default();
    unsafe {
        pav_log_suppress_stderr(1);
        pav_ui_log(subsystem_c.as_ptr(), level, message_c.as_ptr());
        pav_log_suppress_stderr(0);
    }
}
