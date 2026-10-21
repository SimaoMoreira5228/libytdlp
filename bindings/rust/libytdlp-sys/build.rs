use std::env;

fn main() {
    println!("cargo:rerun-if-env-changed=LIBYTDLP_LIB_DIR");

    if let Ok(dir) = env::var("LIBYTDLP_LIB_DIR") {
        println!("cargo:rustc-link-search=native={dir}");

        if env::var("CARGO_CFG_UNIX").is_ok() {
            println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
        }
    }

    println!("cargo:rustc-link-lib=ytdlp");
}
