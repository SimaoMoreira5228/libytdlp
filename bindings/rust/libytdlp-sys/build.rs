use std::env;
use std::path::PathBuf;
use std::process::Command;

fn python_link() {
    let windows = env::var("CARGO_CFG_WINDOWS").is_ok();

    if env::var("CARGO_CFG_UNIX").is_err() && !windows {
        return;
    }

    let default_python = if windows { "python" } else { "python3" };
    let python = env::var("LIBYTDLP_PYTHON").unwrap_or_else(|_| default_python.to_owned());

    if windows {
        let output = Command::new(&python)
            .args([
                "-c",
                "import os, sys; print(os.path.join(sys.base_prefix, 'libs')); print('python%d%d.lib' % sys.version_info[:2])",
            ])
            .output();

        let output = match output {
            Ok(output) if output.status.success() => output,
            _ => return,
        };

        let text = String::from_utf8_lossy(&output.stdout);
        let mut lines = text.lines();

        if let (Some(libdir), Some(file)) = (lines.next(), lines.next()) {
            let stem = file.strip_suffix(".lib").unwrap_or(file);
            println!("cargo:rustc-link-search=native={libdir}");
            println!("cargo:rustc-link-lib={stem}");
        }

        return;
    }

    let output = Command::new(&python)
        .args([
            "-c",
            "import sysconfig; print(sysconfig.get_config_var('LIBDIR')); print('python' + sysconfig.get_config_var('LDVERSION'))",
        ])
        .output();

    let output = match output {
        Ok(output) if output.status.success() => output,
        _ => return,
    };

    let text = String::from_utf8_lossy(&output.stdout);
    let mut lines = text.lines();

    if let (Some(libdir), Some(lib)) = (lines.next(), lines.next()) {
        println!("cargo:rustc-link-search=native={libdir}");
        println!("cargo:rustc-link-lib={lib}");
        println!("cargo:rustc-link-arg=-Wl,-rpath,{libdir}");
        println!("cargo:python_libdir={libdir}");
        println!("cargo:python_lib={lib}");
    }
}

fn dir_has_lib(dir: &std::path::Path) -> bool {
    let names: &[&str] = if env::var("CARGO_CFG_WINDOWS").is_ok() {
        &["ytdlp.lib", "ytdlp_static.lib"]
    } else {
        &["libytdlp.a", "libytdlp.so", "libytdlp.dylib"]
    };

    names.iter().any(|name| dir.join(name).exists())
}

fn repo_root() -> Option<PathBuf> {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").ok()?);

    if manifest.join("CMakeLists.txt").is_file() && manifest.join("native").is_dir() {
        Some(manifest)
    } else {
        None
    }
}

fn collect_sources(dir: &std::path::Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return;
    };

    for entry in entries.flatten() {
        let path = entry.path();

        if path.is_dir() {
            collect_sources(&path, out);
        } else {
            out.push(path);
        }
    }
}

fn native_sources(root: &std::path::Path) -> Vec<PathBuf> {
    let mut sources = vec![
        root.join("CMakeLists.txt"),
        root.join("tools/embed_bridge.py"),
    ];

    collect_sources(&root.join("native"), &mut sources);
    collect_sources(&root.join("include"), &mut sources);
    collect_sources(&root.join("python/libytdlp_bridge"), &mut sources);

    for source in &sources {
        println!("cargo:rerun-if-changed={}", source.display());
    }

    sources
}

fn mtime(path: &std::path::Path) -> Option<std::time::SystemTime> {
    std::fs::metadata(path)
        .and_then(|meta| meta.modified())
        .ok()
}

fn archive_in(dir: &std::path::Path, static_name: &str) -> Option<PathBuf> {
    [dir.to_path_buf(), dir.join("Release"), dir.join("Debug")]
        .into_iter()
        .map(|dir| dir.join(static_name))
        .find(|archive| archive.is_file())
}

fn fresh_repo_build(root: &std::path::Path, static_name: &str) -> Option<PathBuf> {
    let mut candidates = Vec::new();

    if let Some(dir) = env::var_os("BUILD_DIR") {
        candidates.push(PathBuf::from(dir));
    }

    candidates.push(root.join("build"));

    let newest_source = native_sources(root)
        .iter()
        .filter_map(|source| mtime(source))
        .max()?;

    candidates.into_iter().find_map(|dir| {
        let archive = archive_in(&dir, static_name)?;

        let fresh = mtime(&archive).is_some_and(|built| built >= newest_source);

        fresh.then(|| {
            archive
                .parent()
                .expect("archive has a parent")
                .to_path_buf()
        })
    })
}

fn cmake_build(root: &std::path::Path) -> PathBuf {
    println!(
        "cargo:warning=libytdlp: no fresh prebuilt library found, building the native library from {} (needs cmake, a C++23 compiler and Python development headers)",
        root.display()
    );

    let release = env::var("OPT_LEVEL").as_deref() != Ok("0");

    let mut config = cmake::Config::new(root);
    config.define("LIBYTDLP_BUILD_RUST", "OFF");
    config.define("BUILD_TESTING", "OFF");

    if release {
        config.profile("Release");
    } else {
        config.profile("Debug");
    }

    let prefix = config.build();

    ["lib", "lib64"]
        .into_iter()
        .map(|suffix| prefix.join(suffix))
        .find(|dir| dir_has_lib(dir))
        .unwrap_or_else(|| {
            panic!(
                "cmake build succeeded but no library landed in {}",
                prefix.display()
            )
        })
}
fn system_lib_dir() -> Option<PathBuf> {
    if env::var("CARGO_CFG_WINDOWS").is_ok() {
        let path = env::var_os("PATH")?;

        for dir in env::split_paths(&path) {
            for name in ["ytdlp.lib", "ytdlp_static.lib"] {
                if dir.join(name).exists() {
                    return Some(dir);
                }
            }
        }

        return None;
    }

    if env::var("CARGO_CFG_UNIX").is_err() {
        return None;
    }

    for dir in [
        "/usr/lib",
        "/usr/lib64",
        "/usr/local/lib",
        "/usr/local/lib64",
    ] {
        for name in ["libytdlp.so", "libytdlp.dylib", "libytdlp.a"] {
            if PathBuf::from(dir).join(name).exists() {
                return Some(PathBuf::from(dir));
            }
        }
    }

    None
}

fn main() {
    println!("cargo:rerun-if-env-changed=LIBYTDLP_LIB_DIR");
    println!("cargo:rerun-if-env-changed=BUILD_DIR");
    println!("cargo:rerun-if-env-changed=LIBYTDLP_LINK");
    println!("cargo:rerun-if-env-changed=LIBYTDLP_PYTHON");

    let windows = env::var("CARGO_CFG_WINDOWS").is_ok();
    let static_name = if windows {
        "ytdlp_static.lib"
    } else {
        "libytdlp.a"
    };
    let link_name = "ytdlp";
    let static_link_name = if windows { "ytdlp_static" } else { "ytdlp" };

    let dir = match env::var("LIBYTDLP_LIB_DIR").ok().map(PathBuf::from) {
        Some(dir) if dir_has_lib(&dir) => Some(dir),
        Some(dir) => panic!(
            "LIBYTDLP_LIB_DIR holds {} but no built library is in it",
            dir.display()
        ),
        None => match repo_root() {
            Some(root) => {
                Some(fresh_repo_build(&root, static_name).unwrap_or_else(|| cmake_build(&root)))
            }
            None => system_lib_dir(),
        },
    };

    let Some(dir) = dir else {
        panic!(
            "libytdlp native library not found: building from a source checkout needs \
             the repo layout (this copy has no CMakeLists.txt beside it), otherwise \
             install the library system-wide (/usr/lib, /usr/lib64, /usr/local/lib, \
             /usr/local/lib64) or set LIBYTDLP_LIB_DIR to the directory holding \
             libytdlp.so, libytdlp.a (ytdlp.lib or ytdlp_static.lib on Windows)"
        );
    };

    let link = env::var("LIBYTDLP_LINK").unwrap_or_default();
    let want_shared = link == "shared";

    let have_static = dir.join(static_name).exists();

    if link == "static" && !have_static {
        panic!(
            "LIBYTDLP_LINK=static but no {} in {}",
            static_name,
            dir.display()
        );
    }

    println!("cargo:rustc-link-search=native={}", dir.display());

    if env::var("CARGO_CFG_UNIX").is_ok() {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
    }

    if have_static && !want_shared {
        println!("cargo:rerun-if-changed={}/{}", dir.display(), static_name);

        println!("cargo:rustc-link-lib=static={static_link_name}");

        if env::var("CARGO_CFG_UNIX").is_ok() {
            let runtime = match env::var("CARGO_CFG_TARGET_OS").as_deref() {
                Ok("macos") => "c++",
                _ => "stdc++",
            };
            println!("cargo:rustc-link-lib={runtime}");
        }

        python_link();
    } else {
        println!("cargo:rustc-link-lib={link_name}");
    }
}
