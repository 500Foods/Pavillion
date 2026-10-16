// Reads `#define PAVILLION_API_VERSION <digits>` from pavillion.h.
// One definition, a bare decimal integer >= 1, no leading zero.
fn read_api_version(header: &std::path::Path) -> i32 {
    let text = std::fs::read_to_string(header).unwrap_or_else(|err| {
        panic!("cannot read {}: {err}", header.display());
    });
    let mut found: Option<(usize, i32)> = None;
    for (lineno, raw) in text.lines().enumerate() {
        let line = raw.trim();
        let Some(rest) = line.strip_prefix("#define") else {
            continue;
        };
        let mut parts = rest.split_whitespace();
        if parts.next() != Some("PAVILLION_API_VERSION") {
            continue;
        }
        let value = parts.next().unwrap_or("");
        let line_no = lineno + 1;
        if parts.next().is_some()
            || value.is_empty()
            || !value.bytes().all(|byte| byte.is_ascii_digit())
            || (value.len() > 1 && value.starts_with('0'))
        {
            panic!(
                "{}:{line_no}: PAVILLION_API_VERSION must be a bare decimal integer",
                header.display()
            );
        }
        let version: i32 = value.parse().unwrap_or_else(|_| {
            panic!(
                "{}:{line_no}: PAVILLION_API_VERSION does not fit in i32",
                header.display()
            );
        });
        if version < 1 {
            panic!(
                "{}:{line_no}: PAVILLION_API_VERSION must be >= 1",
                header.display()
            );
        }
        if let Some((first, _)) = found {
            panic!(
                "{}:{line_no}: PAVILLION_API_VERSION is also defined on line {first}",
                header.display()
            );
        }
        found = Some((line_no, version));
    }
    found.map(|(_, version)| version).unwrap_or_else(|| {
        panic!(
            "PAVILLION_API_VERSION is missing from {}",
            header.display()
        );
    })
}

fn main() {
    // Release date of this version. Change it when the package version changes.
    let release_date = "2026-10-14";
    println!("cargo:rustc-env=PAVILLION_RELEASE_DATE={release_date}");

    let header = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../core/include/pavillion.h");
    let api_version = read_api_version(&header);
    println!("cargo:rustc-env=PAVILLION_API_VERSION={api_version}");

    let commit = std::process::Command::new("git")
        .args(["rev-parse", "--short=12", "HEAD"])
        .output()
        .ok()
        .filter(|out| out.status.success())
        .and_then(|out| String::from_utf8(out.stdout).ok())
        .map(|text| text.trim().to_string())
        .filter(|text| !text.is_empty() && text.chars().all(|c| c.is_ascii_hexdigit()));
    if let Some(commit) = commit {
        println!("cargo:rustc-env=PAVILLION_GIT_COMMIT={commit}");
    }

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=../.git/HEAD");

    let repo = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("..")
        .canonicalize()
        .expect("repo root");
    let build_dir = repo.join("build");
    let configured = std::process::Command::new("cmake")
        .args(["-S", "core", "-B", "build"])
        .current_dir(&repo)
        .status()
        .expect("failed to run cmake");
    if !configured.success() {
        panic!("cmake -S core -B build failed");
    }
    let built = std::process::Command::new("cmake")
        .args(["--build", "build"])
        .current_dir(&repo)
        .status()
        .expect("failed to run cmake --build");
    if !built.success() {
        panic!("cmake --build build failed");
    }
    println!("cargo:rustc-link-search=native={}", build_dir.display());
    println!("cargo:rustc-link-lib=static=pavillion_core");
    println!("cargo:rustc-link-lib=pthread");
    println!("cargo:rustc-link-lib=jansson");
    println!("cargo:rustc-link-lib=sqlite3");
    println!("cargo:rerun-if-changed=../core/CMakeLists.txt");
    println!("cargo:rerun-if-changed=../core/include/pavillion.h");
    println!("cargo:rerun-if-changed=../core/src/pavillion.c");
    println!("cargo:rerun-if-changed=../core/src/config.c");
    println!("cargo:rerun-if-changed=../core/include/pav_config.h");
    println!("cargo:rerun-if-changed=../core/src/store.c");
    println!("cargo:rerun-if-changed=../core/include/pav_store.h");

    let attributes = tauri_build::Attributes::new()
        .app_manifest(tauri_build::AppManifest::new().commands(&[
            "status",
            "prefs",
            "save_layout",
            "save_icons",
            "save_kit",
            "ui_log",
        ]));
    if let Err(err) = tauri_build::try_build(attributes) {
        panic!("failed to run tauri-build: {err}");
    }
}
