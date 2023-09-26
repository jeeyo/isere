use std::env;

fn main() {
    let target = env::var("TARGET").unwrap_or_default();
    // let target_env = env::var("CARGO_CFG_TARGET_ENV").unwrap_or_default();
    // let target_family = env::var("CARGO_CFG_TARGET_FAMILY").unwrap_or_default();
    // let out_dir = env::var("OUT_DIR").unwrap();

    let mut b = freertos_cargo_build::Builder::new();

    b.freertos("3rdparty/FreeRTOS");
    b.freertos_config("portable/include");
    b.heap("heap_4.c".to_string());

    if target == "x86_64-unknown-linux-gnu" || target == "aarch64-apple-darwin" {
      b.freertos_port("ThirdParty/GCC/Posix".to_string());
    }

    if target == "thumbv6m-none-eabi" {
    //   b.freertos_port("ThirdParty/GCC/RP2040".to_string());
      b.get_cc().include("3rdparty/FreeRTOS/portable/GCC/ARM_CM0");
    }

    b.get_cc().file("src/hooks.c");
    b.get_cc().file("portable/src/tcp.c");

    b.compile().unwrap_or_else(|e| panic!("{}", e.to_string()));
}
