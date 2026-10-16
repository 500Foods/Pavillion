fn main() {
    // Release date of this version. Change it when the package version changes.
    let release_date = "2026-10-14";
    println!("cargo:rustc-env=PAVILLION_RELEASE_DATE={release_date}");

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
