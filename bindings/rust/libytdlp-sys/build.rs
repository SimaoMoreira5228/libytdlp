use std::env;
use std::path::PathBuf;
use std::process::Command;

fn python_link() {
    if env::var("CARGO_CFG_UNIX").is_err() {
        return;
    }

    let python = env::var("LIBYTDLP_PYTHON").unwrap_or_else(|_| "python3".to_owned());

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

fn system_lib_dir() -> Option<PathBuf> {
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
    println!("cargo:rerun-if-env-changed=LIBYTDLP_LINK");
    println!("cargo:rerun-if-env-changed=LIBYTDLP_PYTHON");

    let dir = env::var("LIBYTDLP_LIB_DIR")
        .ok()
        .map(PathBuf::from)
        .or_else(system_lib_dir);

    let Some(dir) = dir else {
        panic!(
            "libytdlp native library not found: install it system-wide \
             or set LIBYTDLP_LIB_DIR to the directory holding libytdlp.so or libytdlp.a"
        );
    };

    let link = env::var("LIBYTDLP_LINK").unwrap_or_default();
    let want_shared = link == "shared";

    let have_static = dir.join("libytdlp.a").exists();

    if link == "static" && !have_static {
        panic!(
            "LIBYTDLP_LINK=static but no libytdlp.a in {}",
            dir.display()
        );
    }

    println!("cargo:rustc-link-search=native={}", dir.display());

    if env::var("CARGO_CFG_UNIX").is_ok() {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", dir.display());
    }

    if have_static && !want_shared {
        println!("cargo:rerun-if-changed={}/libytdlp.a", dir.display());

        println!("cargo:rustc-link-lib=static=ytdlp");

        if env::var("CARGO_CFG_UNIX").is_ok() {
            let runtime = match env::var("CARGO_CFG_TARGET_OS").as_deref() {
                Ok("macos") => "c++",
                _ => "stdc++",
            };
            println!("cargo:rustc-link-lib={runtime}");
        }

        python_link();
    } else {
        println!("cargo:rustc-link-lib=ytdlp");
    }
}
