use std::env;

fn main() {
    println!("cargo:rerun-if-env-changed=LIBYTDLP_LIB_DIR");

    if let Ok(dir) = env::var("LIBYTDLP_LIB_DIR") {
        if env::var("CARGO_CFG_UNIX").is_ok() {
            println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
        }
    }

    // The sys crate reports the Python it linked, but its own link-arg
    // lines do not reach this package's test and binary targets, so the
    // interpreter lookup repeats here where the channel is known to work.
    if let (Ok(libdir), Ok(lib)) = (
        env::var("DEP_YTDLP_PYTHON_LIBDIR"),
        env::var("DEP_YTDLP_PYTHON_LIB"),
    ) {
        println!("cargo:rustc-link-search=native={libdir}");
        println!("cargo:rustc-link-lib={lib}");
        println!("cargo:rustc-link-arg=-Wl,-rpath,{libdir}");
    }
}
