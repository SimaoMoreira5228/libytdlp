use std::env;
use std::path::Path;
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

fn main() {
    println!("cargo:rerun-if-env-changed=LIBYTDLP_LIB_DIR");
    println!("cargo:rerun-if-env-changed=LIBYTDLP_LINK");
    println!("cargo:rerun-if-env-changed=LIBYTDLP_PYTHON");

    let dir = env::var("LIBYTDLP_LIB_DIR").ok();
    let link = env::var("LIBYTDLP_LINK").unwrap_or_default();
    let want_shared = link == "shared";

    let have_static = dir
        .as_ref()
        .map(|dir| Path::new(dir).join("libytdlp.a").exists())
        .unwrap_or(false);

    if link == "static" && !have_static {
        panic!("LIBYTDLP_LINK=static but no libytdlp.a in LIBYTDLP_LIB_DIR");
    }

    if let Some(dir) = dir.as_deref() {
        println!("cargo:rustc-link-search=native={dir}");

        if env::var("CARGO_CFG_UNIX").is_ok() {
            println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
        }
    }

    if have_static && !want_shared {
        if let Some(dir) = dir.as_deref() {
            println!("cargo:rerun-if-changed={dir}/libytdlp.a");
        }

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
