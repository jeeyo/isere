#![no_std]
#![feature(error_in_core)]
#![feature(core_panic)]

#[global_allocator]
static GLOBAL: FreeRtosAllocator = FreeRtosAllocator;

// mod js;
// mod httpd;

use freertos_rust::*;
use log::error;

// use js::Js;

extern "C" {
  pub fn tcp_init() -> cty::c_int;
  pub fn tcp_deinit() -> cty::c_int;
}

fn main() {
//   env_logger::Builder::new()
//     .filter(None, LevelFilter::Info)
//     .init();

  if unsafe { tcp_init() } != 0 {
    error!("failed to init tcp");
    return;
  }

//   let response = match Js::eval("
// export const handler = async function(event, context, done) {
//   console.log('Test ESM')
//   console.log('## ENVIRONMENT VARIABLES: ', process.env)
//   console.log('## CONTEXT: ', context)
//   console.log('## EVENT: ', event)

//   // setTimeout(() => {
//   //   console.log('ESM Inside')
//   // }, 5000)

//   // done({
//   //   statusCode: 200,
//   //   headers: { 'Content-Type': 'text/plain' },
//   //   body: { k: 'j' }
//   // })

//   // const a = await new Promise(resolve => resolve('555'));
//   // console.log('a', a)

//   return {
//     statusCode: 404,
//     headers: { 'Content-Type': 'text/plain' },
//     body: 123
//   }
// }

// console.log('ESM Outside')
//         ") {
//     Ok(resp) => resp,
//     Err(e) => {
//       error!("{}", e);
//       return;
//     },
//   };

//   info!("{:?}", response);

  FreeRtosUtils::start_scheduler();

  // if unsafe { tcp_deinit() } != 0 {
  //   error!("failed to deinit tcp");
  //   return;
  // }
}
