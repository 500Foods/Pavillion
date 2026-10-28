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

// One directory name under the repo. `make mini` uses `build-mini` so a size
// configure cannot rewrite the `build/` cache `make build` is using.
fn cmake_dir_name() -> String {
    let name = std::env::var("PAVILLION_CMAKE_DIR").unwrap_or_else(|_| "build".to_string());
    let ok = !name.is_empty()
        && name
            .chars()
            .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
        && !name.starts_with('-');
    if !ok {
        panic!("PAVILLION_CMAKE_DIR must be a single directory name");
    }
    name
}

// PAVILLION_BUILD is one positive integer at the repo root. The package
// version stays 0.phase.0. The shown version is 0.phase.<that count>.
fn read_build_number() -> String {
    let path = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap())
        .join("../PAVILLION_BUILD");
    println!("cargo:rerun-if-changed=../PAVILLION_BUILD");
    let text = std::fs::read_to_string(&path).unwrap_or_else(|err| {
        panic!("cannot read {}: {err}", path.display());
    });
    let value = text.trim();
    if value.is_empty()
        || !value.bytes().all(|byte| byte.is_ascii_digit())
        || value.starts_with('0')
    {
        panic!("{} must be a positive integer", path.display());
    }
    value.to_string()
}

fn shown_version(build: &str) -> String {
    let pkg = std::env::var("CARGO_PKG_VERSION").expect("CARGO_PKG_VERSION");
    let mut parts = pkg.split('.');
    let major = parts.next().unwrap_or("");
    let phase = parts.next().unwrap_or("");
    let rest = parts.next().unwrap_or("");
    if major != "0"
        || phase.is_empty()
        || !phase.bytes().all(|byte| byte.is_ascii_digit())
        || rest != "0"
        || parts.next().is_some()
    {
        panic!("package version must be 0.phase.0, got {pkg}");
    }
    format!("{major}.{phase}.{build}")
}

fn main() {
    // Release date of this phase stamp. Change it when the middle number changes.
    let release_date = "2026-10-24";
    println!("cargo:rustc-env=PAVILLION_RELEASE_DATE={release_date}");
    let build_number = read_build_number();
    println!("cargo:rustc-env=PAVILLION_BUILD={build_number}");
    println!(
        "cargo:rustc-env=PAVILLION_VERSION={}",
        shown_version(&build_number)
    );

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
    let dir_name = cmake_dir_name();
    let size = std::env::var("PAVILLION_SIZE").ok().as_deref() == Some("1");
    if size && dir_name == "build" {
        panic!("PAVILLION_SIZE refuses to reconfigure build/; set PAVILLION_CMAKE_DIR");
    }
    println!("cargo:rerun-if-env-changed=PAVILLION_CMAKE_DIR");
    println!("cargo:rerun-if-env-changed=PAVILLION_SIZE");
    let mut configure = std::process::Command::new("cmake");
    configure
        .args(["-S", "core", "-B", &dir_name])
        .current_dir(&repo);
    if size {
        let opt = size_opt_flag();
        configure.arg("-DCMAKE_BUILD_TYPE=MinSizeRel").arg(format!(
            "-DCMAKE_C_FLAGS_MINSIZEREL={opt} -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -DNDEBUG"
        ));
    }
    let configured = configure.status().expect("failed to run cmake");
    if !configured.success() {
        panic!("cmake -S core -B {dir_name} failed");
    }
    let built = std::process::Command::new("cmake")
        .args(["--build", &dir_name])
        .current_dir(&repo)
        .status()
        .expect("failed to run cmake --build");
    if !built.success() {
        panic!("cmake --build {dir_name} failed");
    }
    let build_dir = repo.join(&dir_name);
    println!("cargo:rustc-link-search=native={}", build_dir.display());
    println!("cargo:rustc-link-lib=static=pavillion_core");
    println!("cargo:rustc-link-search=native={}", build_dir.join("lexbor").display());
    println!("cargo:rustc-link-lib=static:+whole-archive=lexbor_static");
    println!("cargo:rustc-link-lib=pthread");
    println!("cargo:rustc-link-lib=jansson");
    println!("cargo:rustc-link-lib=sqlite3");
    // srv_lookup (G Lock): the target OS picks the DNS library.
    match std::env::var("CARGO_CFG_TARGET_OS").as_deref() {
        Ok("linux") | Ok("macos") => {
            println!("cargo:rustc-link-lib=resolv");
            println!("cargo:rustc-link-lib=m");
        }
        Ok("windows") => println!("cargo:rustc-link-lib=dnsapi"),
        _ => {}
    }
    /* iconv is in glibc. Apple and MinGW need the library named. */
    match std::env::var("CARGO_CFG_TARGET_OS").as_deref() {
        Ok("macos") | Ok("windows") => println!("cargo:rustc-link-lib=iconv"),
        _ => {}
    }
    if cfg!(target_os = "linux") {
        if let Ok(libs) = std::process::Command::new("pkg-config")
            .args(["--libs", "libsecret-1"])
            .output()
        {
            if libs.status.success() {
                let text = String::from_utf8_lossy(&libs.stdout);
                for flag in text.split_whitespace() {
                    if let Some(lib) = flag.strip_prefix("-l") {
                        println!("cargo:rustc-link-lib={lib}");
                    } else if let Some(path) = flag.strip_prefix("-L") {
                        println!("cargo:rustc-link-search=native={path}");
                    }
                }
            }
        }
    }
    // curl and GMime are needed by the C core on every target (MSYS2 pkg-config on Windows).
        if let Ok(libs) = std::process::Command::new("pkg-config")
            .args(["--libs", "libcurl"])
            .output()
        {
            if libs.status.success() {
                let text = String::from_utf8_lossy(&libs.stdout);
                for flag in text.split_whitespace() {
                    if let Some(lib) = flag.strip_prefix("-l") {
                        println!("cargo:rustc-link-lib={lib}");
                    } else if let Some(path) = flag.strip_prefix("-L") {
                        println!("cargo:rustc-link-search=native={path}");
                    }
                }
            }
        }
        let mut pc_path = std::env::var("PKG_CONFIG_PATH").unwrap_or_default();
        if let (true, Ok(home)) = (cfg!(target_os = "linux"), std::env::var("HOME")) {
            let extra = format!("{home}/.local/lib/pkgconfig");
            if pc_path.is_empty() {
                pc_path = extra;
            } else if !pc_path.split(':').any(|p| p == extra) {
                pc_path = format!("{extra}:{pc_path}");
            }
        }
        if let Ok(libs) = std::process::Command::new("pkg-config")
            .args(["--libs", "gmime-3.0"])
            .env("PKG_CONFIG_PATH", &pc_path)
            .output()
        {
            if libs.status.success() {
                let text = String::from_utf8_lossy(&libs.stdout);
                for flag in text.split_whitespace() {
                    if let Some(lib) = flag.strip_prefix("-l") {
                        println!("cargo:rustc-link-lib={lib}");
                    } else if let Some(path) = flag.strip_prefix("-L") {
                        println!("cargo:rustc-link-search=native={path}");
                    }
                }
            }
        }
    if cfg!(target_os = "linux") {
        if let Ok(dir) = std::process::Command::new("pkg-config")
            .args(["--variable=libdir", "gmime-3.0"])
            .env("PKG_CONFIG_PATH", &pc_path)
            .output()
        {
            if dir.status.success() {
                let text = String::from_utf8_lossy(&dir.stdout);
                let libdir = text.trim();
                // A system libdir needs no runpath; Fedora rpmbuild QA rejects
                // a standard one like /usr/lib64 (check-rpaths 0x0001).
                let system = matches!(libdir, "/usr/lib64" | "/usr/lib" | "/lib64" | "/lib")
                    || libdir.starts_with("/usr/lib/x86_64-linux-gnu");
                if !libdir.is_empty() && !system {
                    println!("cargo:rustc-link-arg=-Wl,-rpath,{libdir}");
                }
            }
        }
        if let Ok(ver) = std::process::Command::new("pkg-config")
            .args(["--modversion", "webkit2gtk-4.1"])
            .output()
        {
            if ver.status.success() {
                let text = String::from_utf8_lossy(&ver.stdout);
                let v = text.trim();
                if !v.is_empty() {
                    println!("cargo:rustc-env=WEBKITGTK_VERSION={v}");
                }
            }
        }
    }
    println!("cargo:rerun-if-changed=../core/CMakeLists.txt");
    println!("cargo:rerun-if-changed=../core/include/pavillion.h");
    println!("cargo:rerun-if-changed=../core/include/pav_net.h");
    println!("cargo:rerun-if-changed=../core/src/pavillion.c");
    println!("cargo:rerun-if-changed=../core/src/net.c");
    println!("cargo:rerun-if-changed=../core/src/config.c");
    println!("cargo:rerun-if-changed=../core/src/config_prefs.c");
    println!("cargo:rerun-if-changed=../core/src/config_fs.c");
    println!("cargo:rerun-if-changed=../core/src/config_internal.h");
    println!("cargo:rerun-if-changed=../core/src/cfg.c");
    println!("cargo:rerun-if-changed=../core/include/pav_config.h");
    println!("cargo:rerun-if-changed=../core/include/pav_cfg.h");
    println!("cargo:rerun-if-changed=../core/src/store.c");
    println!("cargo:rerun-if-changed=../core/include/pav_store.h");
    println!("cargo:rerun-if-changed=../core/include/pav_platform.h");
    println!("cargo:rerun-if-changed=../core/platform/platform_internal.h");
    println!("cargo:rerun-if-changed=../core/platform/posix/posix.c");
    println!("cargo:rerun-if-changed=../core/platform/linux/linux.c");
    println!("cargo:rerun-if-changed=../core/platform/linux/secrets.c");
    println!("cargo:rerun-if-changed=../core/platform/fake/fake.c");
    println!("cargo:rerun-if-changed=../core/platform/fake/host.c");

    let attributes = tauri_build::Attributes::new()
        .app_manifest(tauri_build::AppManifest::new().commands(&["core"]));
    if let Err(err) = tauri_build::try_build(attributes) {
        panic!("failed to run tauri-build: {err}");
    }
}

/// `-Oz` where the C compiler accepts it (GCC 12+, clang); `-Os` otherwise
/// (GCC 11 on ubuntu-22.04 for the AppImage). If the probe compiler cannot be
/// spawned at all, keep `-Oz` so existing rpm/msi builds are unchanged.
fn size_opt_flag() -> &'static str {
    println!("cargo:rerun-if-env-changed=CC");
    let cc = std::env::var("CC").unwrap_or_else(|_| "cc".into());
    let out = std::path::PathBuf::from(std::env::var("OUT_DIR").expect("OUT_DIR"));
    let src = out.join("oz_probe.c");
    std::fs::write(&src, "int main(void){return 0;}\n").expect("write oz_probe.c");
    match std::process::Command::new(&cc)
        .arg("-Oz")
        .arg("-c")
        .arg(&src)
        .arg("-o")
        .arg(out.join("oz_probe.o"))
        .stderr(std::process::Stdio::null())
        .status()
    {
        Ok(s) if !s.success() => "-Os",
        _ => "-Oz",
    }
}
