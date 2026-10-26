#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn version_line() -> String {
    format!(
        "pavillion {} ({})",
        env!("PAVILLION_VERSION"),
        env!("PAVILLION_RELEASE_DATE")
    )
}

fn help_text() -> String {
    let mut text = version_line();
    text.push('\n');
    if let Some(commit) = option_env!("PAVILLION_GIT_COMMIT") {
        if !commit.is_empty() {
            text.push_str("commit ");
            text.push_str(commit);
            text.push('\n');
        }
    }
    text.push_str("\nUsage: pavillion [--selftest] [WORKDIR]\n");
    text
}

/// Flags leave before the window starts. The core is not called.
fn cli_exit_code() -> Option<i32> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let flags: Vec<&str> = args
        .iter()
        .filter(|arg| arg.starts_with('-'))
        .map(String::as_str)
        .collect();
    if flags.is_empty() {
        return None;
    }
    if args.len() == 1 && flags[0] == "--help" {
        print!("{}", help_text());
        return Some(0);
    }
    if args.len() == 1 && flags[0] == "--version" {
        println!("{}", version_line());
        return Some(0);
    }
    let selftest = flags.iter().filter(|flag| **flag == "--selftest").count();
    if selftest == 1 && selftest == flags.len() && args.len() - selftest <= 1 {
        return None;
    }
    eprint!("{}", help_text());
    Some(1)
}

/// The core reads these once in pavillion_start. A release build clears a
/// debug value inherited from the environment. main is the only thread here,
/// which is why the edition-2024 set_var calls are safe.
fn publish_host_flags(selftest: bool) {
    unsafe {
        if cfg!(debug_assertions) {
            std::env::set_var("PAVILLION_DEBUG", "1");
        } else {
            std::env::remove_var("PAVILLION_DEBUG");
        }
        if selftest {
            std::env::set_var("PAVILLION_SELFTEST", "1");
        } else {
            std::env::remove_var("PAVILLION_SELFTEST");
        }
    }
}

fn default_workdir() -> String {
    match std::env::var("HOME") {
        Ok(home) if !home.is_empty() => format!("{home}/.local/share/pavillion"),
        _ => {
            pavillion_lib::core_log(0, "HOME is not set");
            std::process::exit(1);
        }
    }
}

fn main() {
    pavillion_lib::install_native_stderr_capture();
    pavillion_lib::block_quit_signals();
    if let Some(ver) = option_env!("WEBKITGTK_VERSION") {
        if !ver.is_empty() {
            let line = format!("library loaded:  WebKitGTK {}", ver);
            pavillion_lib::core_log(4, &line);
        }
    }
    if let Some(code) = cli_exit_code() {
        std::process::exit(code);
    }
    let args: Vec<String> = std::env::args().skip(1).collect();
    let selftest = args.iter().any(|arg| arg == "--selftest");
    let positionals: Vec<String> = args.into_iter().filter(|arg| arg != "--selftest").collect();
    if positionals.len() > 1 {
        std::process::exit(pavillion_lib::unknown_subcommand());
    }
    publish_host_flags(selftest);
    let workdir = if positionals.len() == 1 {
        positionals[0].clone()
    } else {
        default_workdir()
    };
    pavillion_lib::run(&workdir);
}
