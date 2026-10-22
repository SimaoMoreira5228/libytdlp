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

fn dev_tree_dir() -> Option<PathBuf> {
    if let Some(dir) = env::var_os("BUILD_DIR") {
        let dir = PathBuf::from(dir);

        if dir_has_lib(&dir) {
            return Some(dir);
        }
    }

    if let Ok(manifest) = env::var("CARGO_MANIFEST_DIR") {
        let dir = PathBuf::from(manifest)
            .ancestors()
            .nth(3)
            .map(|root| root.join("build"))
            .filter(|dir| dir_has_lib(dir));

        if dir.is_some() {
            return dir;
        }
    }

    None
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
        None => dev_tree_dir().or_else(system_lib_dir),
    };

    let Some(dir) = dir else {
        panic!(
            "libytdlp native library not found: build the repo first (cargo finds \
             <root>/build by itself) or install it system-wide (/usr/lib, /usr/lib64, \
             /usr/local/lib, /usr/local/lib64), or set LIBYTDLP_LIB_DIR to the \
             directory holding libytdlp.so, libytdlp.a \
             (ytdlp.lib or ytdlp_static.lib on Windows)"
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
