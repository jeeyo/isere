@0xe62236a3958b963b;

# using C = import "3rdparty/c-capnproto/compiler/c.capnp";
# $C.fieldgetset;

struct Config {
  path @0 :Text;
  timeout @1 :Int32;
}
