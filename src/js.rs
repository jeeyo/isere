use rquickjs::{Context, Runtime, Module, context::intrinsic, Object, Function, loader::{BuiltinResolver, BuiltinLoader}};
use core::{error::Error};
use core::fmt;
use log::{error, info, warn};

extern crate alloc;
use alloc::{string::String, vec::Vec, format};

/*
  Response object:
  ```
  {
    "isBase64Encoded": false, // Set to `true` for binary support.
    "statusCode": 200,
    "headers": {
        "header1Name": "header1Value",
        "header2Name": "header2Value",
    },
    "body": "...",
  }
  ```
*/
#[derive(Debug)]
pub struct Response {
  is_base64_encoded: bool,
  status_code: u32,
  headers: Vec<(String, String)>,
  body: String,
}

// impl fmt::Display for Response {
//   fn fmt(&self, 
// }

#[derive(Debug)]
pub struct JsError {
  message: String,
}

impl fmt::Display for JsError {
  fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
    write!(f, "{}", self.message)
  }
}

impl Error for JsError {
  fn description(&self) -> &str {
    &self.message
  }
}

pub struct Js {
  ctx: Context,
}

impl Js {
  pub const ISERE_JS_STACK_SIZE: usize = 65536;
  pub const ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME: &'static str = "__response";

  pub fn init() -> Result<Js, JsError> {
    let rt = match Runtime::new() {
      Ok(rt) => rt,
      Err(_e) => return Err(JsError { message: String::from("failed to create QuickJS runtime") }),
    };

    let ctx = match Context::builder()
      .with::<intrinsic::BaseObjects>()
      .with::<intrinsic::Date>()
      .with::<intrinsic::Eval>()
      .with::<intrinsic::StringNormalize>()
      .with::<intrinsic::RegExp>()
      .with::<intrinsic::Json>()
      .with::<intrinsic::Proxy>()
      .with::<intrinsic::MapSet>()
      .with::<intrinsic::TypedArrays>()
      .with::<intrinsic::Promise>()
      .with::<intrinsic::BigInt>()
      .build(&rt) {
        Ok(ctx) => ctx,
        Err(_e) => return Err(JsError { message: String::from("failed to create QuickJS context") }),
    };

    // TODO: dynamically link this
    let resolver = BuiltinResolver::default().with_module("handler");
    let loader = BuiltinLoader::default().with_module("handler",
      "
export const handler = async function(event, context, done) {
  console.log('Test ESM')
  console.log('## ENVIRONMENT VARIABLES: ', process.env)
  console.log('## CONTEXT: ', context)
  console.log('## EVENT: ', event)

  // setTimeout(() => {
  //   console.log('ESM Inside')
  // }, 5000)

  // done({
  //   statusCode: 200,
  //   headers: { 'Content-Type': 'text/plain' },
  //   body: { k: 'j' }
  // })

  // const a = await new Promise(resolve => resolve('555'));
  // console.log('a', a)

  return {
    statusCode: 404,
    headers: { 'Content-Type': 'text/plain' },
    body: { k: 'v' }
  }
}
      ");

    rt.set_loader(resolver, loader);

    // TODO: set global memory limit with JS_SetMemoryLimit()
    rt.set_max_stack_size(Self::ISERE_JS_STACK_SIZE);

    ctx.with(|ctx| {
      // add console.log(), console.warn(), and console.error() function
      let console = Object::new(ctx.clone()).unwrap();

      let log = Function::new(ctx.clone(), |msg: String| { info!("{msg}"); }).unwrap().with_name("log").unwrap();
      let warn = Function::new(ctx.clone(), |msg: String| { warn!("{msg}"); }).unwrap().with_name("warn").unwrap();
      let error = Function::new(ctx.clone(), |msg: String| { error!("{msg}"); }).unwrap().with_name("error").unwrap();

      console.set("log", log).unwrap();
      console.set("warn", warn).unwrap();
      console.set("error", error).unwrap();
      ctx.globals().set("console", console).unwrap();

      // add process.env
      let envvars = Object::new(ctx.clone()).unwrap();
      // envvars.set("NODE_ENV", env::var("NODE_ENV").unwrap_or("Development".to_string())).unwrap();
      envvars.set("NODE_ENV", "Production").unwrap();

      let process = Object::new(ctx.clone()).unwrap();
      process.set("env", envvars).unwrap();
      ctx.globals().set("process", process).unwrap();

      // add polyfills
      // TODO: timer polyfill
      // TOOD: fetch polyfill
    });

    Ok(Js { ctx })
  }

  pub fn eval(&self) -> Result<(), JsError> {
    match self.ctx.with(|ctx| -> Result<(), JsError> {
      match Module::evaluate(ctx, "<isere>",
        "
import { handler } from 'handler'
globalThis.__test = { 'iam': 'jeeyo' }
console.log('test')
const handler1 = new Promise(async resolve => resolve(await handler(__event, __context, resolve)))
Promise.resolve(handler1).then((response) => {
  globalThis.__response = response;
  console.log('response');
})
console.log('bro');
        "
      ) {
        Ok(_) => return Ok(()),
        Err(e) => return Err(JsError { message: format!("failed to evaluate module: {}", e) }),
      }
    }) {
      Ok(_) => (),
      Err(e) => { return Err(e); },
    };

    let _ = self.ctx.runtime().execute_pending_job().unwrap();

    self.ctx.with(|ctx| {
      info!("{:?}", ctx.globals().get::<_, Object>("__test").unwrap().get::<_, String>("iam").unwrap());
    });

    // let resp = self.ctx.with(|ctx| -> Response {
    //   Response {
    //     is_base64_encoded: module.get::<_, bool>("isBase64Encoded").unwrap_or(false),
    //     status_code: module.get::<_, u32>("statusCode").unwrap_or(200),
    //     headers: module.get::<_, Object>("headers").unwrap_or(Object::new(ctx.clone()).unwrap())
    //       .props()
    //       .collect::<Result<Vec<(String, String)>, _>>()
    //       .unwrap(),
    //     body: module.get::<_, String>("body").unwrap_or(String::from("")),
    //   }
    // });

    Ok(())
  }
}
