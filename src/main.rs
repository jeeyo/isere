#![no_std]
#![feature(error_in_core)]

mod js;
mod log;

use js::Js;

fn main() {
  let js = match Js::init(|| {}) {
    Ok(js) => js,
    Err(e) => panic!("{}", e),
  };
}
