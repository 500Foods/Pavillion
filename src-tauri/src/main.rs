#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn version_line() -> String {
    format!(
        "pavillion {} ({})",
        env!("CARGO_PKG_VERSION"),
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
    text.push_str("\nUsage: pavillion [WORKDIR]\n");
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
    eprint!("{}", help_text());
    Some(1)
}

fn default_workdir() -> String {
    match std::env::var("HOME") {
        Ok(home) if !home.is_empty() => format!("{home}/.local/share/pavillion"),
        _ => {
            eprintln!("HOME is not set");
            std::process::exit(1);
        }
    }
}

fn main() {
    if let Some(code) = cli_exit_code() {
        std::process::exit(code);
    }
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.len() > 1 {
        eprint!("{}", help_text());
        std::process::exit(1);
    }
    let workdir = if args.len() == 1 {
        args[0].clone()
    } else {
        default_workdir()
    };
    pavillion_lib::run(&workdir);
}
