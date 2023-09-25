use rquickjs::{Context, Runtime, Module, context::intrinsic, Object, Function, loader::{BuiltinResolver, BuiltinLoader}};
use core::error::Error;
use core::fmt;
use log::{error, info, warn};

use crate::httpd::Response;

extern crate alloc;
use alloc::{string::String, vec::Vec, format};

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

pub struct Js {}

impl Js {
  pub const ISERE_JS_STACK_SIZE: usize = 65536;
  pub const ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME: &'static str = "__response";
  const ISERE_JS_EVENT_OBJ_NAME: &'static str = "__event";
  // const ISERE_JS_CONTEXT_OBJ_NAME: &'static str = "__context";

  pub fn eval(source: &str) -> Result<Response, JsError> {
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

    // TODO: set global memory limit with JS_SetMemoryLimit()
    rt.set_max_stack_size(Self::ISERE_JS_STACK_SIZE);

    ctx.with(|ctx| {
      // add console.log(), console.warn(), and console.error() function
      {
        let console = Object::new(ctx.clone()).unwrap();

        // enum StringOrObject {
        //   String(String),
        //   Object(Object),
        // }
        // let log = Function::new(ctx.clone(), |msg: StringOrObject| { info!("{}", msg.as_string().unwrap().to_string().unwrap()); }).unwrap().with_name("log").unwrap();
        let log = Function::new(ctx.clone(), |msg: String| { info!("{msg}"); }).unwrap().with_name("log").unwrap();
        let warn = Function::new(ctx.clone(), |msg: String| { warn!("{msg}"); }).unwrap().with_name("warn").unwrap();
        let error = Function::new(ctx.clone(), |msg: String| { error!("{msg}"); }).unwrap().with_name("error").unwrap();

        console.set("log", log).unwrap();
        console.set("warn", warn).unwrap();
        console.set("error", error).unwrap();

        ctx.globals().set("console", console).unwrap();
      }

      // add process.env
      {
        let envvars = Object::new(ctx.clone()).unwrap();
        // envvars.set("NODE_ENV", env::var("NODE_ENV").unwrap_or("Development".to_string())).unwrap();
        envvars.set("NODE_ENV", "Production").unwrap();

        let process = Object::new(ctx.clone()).unwrap();
        process.set("env", envvars).unwrap();

        ctx.globals().set("process", process).unwrap();
      }

      // add `event` object: https://aws-lambda-for-python-developers.readthedocs.io/en/latest/02_event_and_context/
      {
        // TODO: HTTP Request
        let event = Object::new(ctx.clone()).unwrap();
        event.set("httpMethod", "GET").unwrap();
        event.set("path", "/").unwrap();

        // TODO: multi-value headers
        let headers = Object::new(ctx.clone()).unwrap();
        headers.set("Content-Type", "text/plain").unwrap();
        event.set("headers", headers).unwrap();

        // TODO: query string params
        // TODO: multi-value query string params
        event.set("query", "").unwrap();

        // TODO: check `Content-Type: application/json`
        // JSValue parsedBody = JS_ParseJSON(js.context, body, strlen(body), "<input>");
        // if (!JS_IsObject(parsedBody)) {
        //   JS_FreeValue(js.context, parsedBody);
        //   parsedBody = JS_NewString(js.context, body);
        // }
        // JS_SetPropertyStr(js.context, event, "body", parsedBody);
        event.set("body", "").unwrap();

        // TODO: binary body
        event.set("isBase64Encoded", false).unwrap();

        ctx.globals().set(Self::ISERE_JS_EVENT_OBJ_NAME, event).unwrap();
      }

      // add polyfills
      // TODO: timer polyfill
      // TOOD: fetch polyfill
    });

    // TODO: dynamically link this
    let resolver = BuiltinResolver::default().with_module("handler");
    let loader = BuiltinLoader::default().with_module("handler", source);

    ctx.runtime().set_loader(resolver, loader);

    ctx.with(|ctx| {
      let _ = Module::evaluate(ctx, "<isere>",
        format!("
import {{ handler }} from 'handler'
// Promise.resolve(handler(__context, __context)).then((response) => {{
Promise.resolve(handler({})).then((response) => {{
  globalThis.__response = response
}})
        ", Self::ISERE_JS_EVENT_OBJ_NAME)
      );
    });

    while ctx.runtime().execute_pending_job().unwrap() {}

    let resp = ctx.with(|ctx| -> Response {
      let res = ctx.globals().get::<_, Object>(Self::ISERE_JS_HANDLER_FUNCTION_RESPONSE_OBJ_NAME).unwrap();
      Response {
        is_base64_encoded: res.get::<_, bool>("isBase64Encoded").unwrap_or(false),
        status_code: res.get::<_, u32>("statusCode").unwrap_or(200),
        headers: res.get::<_, Object>("headers").unwrap_or(
          Object::new(ctx.clone())
          .unwrap())
          .props()
          .collect::<Result<Vec<(String, String)>, _>>()
          .unwrap(),
        body: res.get::<_, String>("body")
          .unwrap_or(
            ctx.json_stringify(
              res.get::<_, Object>("body")
                .unwrap_or(
                  Object::new(ctx.clone()).unwrap()
                )
              )
              .unwrap()
              .unwrap()
              .to_string()
              .unwrap()
          ),
      }
    });

    Ok(resp)
  }
}
