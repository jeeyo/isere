use rquickjs::{Context, Runtime, context::intrinsic, Object, Function, String as QjsString};
use core::error::Error;
use core::fmt;
use heapless::{String, Vec};

const MAX_HTTP_HEADERS: usize = 20;
const MAX_HTTP_HEADER_NAME_LEN: usize = 64;
const MAX_HTTP_HEADER_VALUE_LEN: usize = 1024;
const MAX_HTTP_BODY_LEN: usize = 2048;

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
pub struct Response {
  is_base64_encoded: bool,
  status_code: u32,
  headers: Vec<(String<MAX_HTTP_HEADER_NAME_LEN>, String<MAX_HTTP_HEADER_VALUE_LEN>), MAX_HTTP_HEADERS>,
  body: String<2048>,
}

#[derive(Debug)]
pub struct JsError {
  message: String<128>,
}

impl fmt::Display for JsError {
  fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
    write!(f, "")
  }
}

impl Error for JsError {
  fn description(&self) -> &str {
    &self.message
  }
}

type Callback = fn();

pub struct Js {
  rt: Runtime,
  ctx: Context,
  callback: Callback,
}

impl Js {
  const ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME: &'static str = "__response";

  pub fn init(callback: Callback) -> Result<Js, JsError> {
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

    // rt.set_max_stack_size(limit);

    ctx.with(|ctx| {
      // add console.log(), console.warn(), and console.error() function
      let console = Object::new(ctx).unwrap();

      // TODO: no_std logging
      // let log = Function::new(ctx.clone(), |msg: QjsString| { info!("{}", msg); }).unwrap();
      // let warn = Function::new(ctx.clone(), |msg: QjsString| { warn!("{}", msg); }).unwrap();
      // let error = Function::new(ctx.clone(), |msg: QjsString| { error!("{}", msg); }).unwrap();
      let log = Function::new(ctx.clone(), |_msg: QjsString| { }).unwrap();
      let warn = Function::new(ctx.clone(), |_msg: QjsString| { }).unwrap();
      let error = Function::new(ctx.clone(), |_msg: QjsString| { }).unwrap();

      console.set("log", log).unwrap();
      console.set("warn", warn).unwrap();
      console.set("error", error).unwrap();
      ctx.globals().set("console", console).unwrap();

      // add process.env
      let envvars = Object::new(ctx).unwrap();
      // envvars.set("NODE_ENV", env::var("NODE_ENV").unwrap_or("Development".to_string())).unwrap();
      envvars.set("NODE_ENV", "Production").unwrap();

      let process = Object::new(ctx).unwrap();
      process.set("env", envvars).unwrap();
      ctx.globals().set("process", process).unwrap();

      // add polyfills
      // TODO: timer polyfill
      // TOOD: fetch polyfill
    });

    Ok(Js { rt, ctx, callback })
  }

  pub fn eval(&self, _js: &str) -> Result<Response, JsError> {
    let val = match self.ctx.with(|ctx| -> Result<Response, JsError> {
      // TODO: dynamically link this
      let handler = match ctx.compile("handler",
      "export const handler = async function(event, context, done) {
          console.log('Test ESM')
          console.log('## ENVIRONMENT VARIABLES: ', process.env)
          console.log('## CONTEXT: ', context)
          console.log('## EVENT: ', event)

          setTimeout(() => {
            console.log('ESM Inside')
          }, 5000)

          // done({
          //   statusCode: 200,
          //   headers: { 'Content-Type': 'text/plain' },
          //   body: { k: 'j' }
          // })

          const a = await new Promise(resolve => resolve('555'));
          console.log('a', a)

          return {
            statusCode: 404,
            headers: { 'Content-Type': 'text/plain' },
            body: { k: 'v' }
          }
        }

        console.log('ESM Outside')"
      ) {
        Ok(handler) => handler,
        Err(_e) => return Err(JsError { message: String::from("failed to compile handler module") }),
      };

      unsafe {
        match handler.eval() {
          Ok(_) => (),
          Err(_e) => return Err(JsError { message: String::from("failed to evaluate handler module") }),
        }
      };

      let val: Object = ctx.eval(
        "import { handler } from 'handler';
        const handler1 = new Promise(async resolve => resolve(await handler(__event, __context, resolve)))
        Promise.resolve(handler1).then(cb)"
      ).unwrap();

      let headers = val.get::<_, Object>("headers").unwrap().props();

      Ok(Response {
        is_base64_encoded: val.get::<_, bool>("isBase64Encoded").unwrap(),
        status_code: val.get::<_, u32>("statusCode").unwrap(),
        headers: val.get::<_, Object>("headers").unwrap()
          .props()
          .collect::<Result<Vec<(String<MAX_HTTP_HEADER_NAME_LEN>, String<MAX_HTTP_HEADER_VALUE_LEN>), MAX_HTTP_HEADERS>, _>>()
          .unwrap(),
        body: val.get::<_, String<MAX_HTTP_BODY_LEN>>("body").unwrap(),
      })
    }) {
      Ok(val) => val,
      Err(e) => return Err(e),
    };

    let _ = self.rt.execute_pending_job();
    Ok(val)
  }
}
