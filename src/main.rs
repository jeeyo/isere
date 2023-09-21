#![no_std]
#![feature(error_in_core)]
#![feature(core_panic)]

#[global_allocator]
static GLOBAL: FreeRtosAllocator = FreeRtosAllocator;

mod js;

use freertos_rust::*;
use log::{LevelFilter, error};

use js::Js;

fn main() {
  env_logger::Builder::new()
    .filter(None, LevelFilter::Info)
    .init();

  let js = match Js::init() {
    Ok(js) => js,
    Err(e) => {
      error!("{}", e);
      return;
    },
  };

  match js.eval() {
    Ok(_) => (),
    Err(e) => {
      error!("{}", e);
      return;
    },
  }

  // println!("{:?}", response);
}
