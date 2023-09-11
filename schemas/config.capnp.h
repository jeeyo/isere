#ifndef CAPN_E62236A3958B963B
#define CAPN_E62236A3958B963B
/* AUTO GENERATED - DO NOT EDIT */
#include <capnp_c.h>

#if CAPN_VERSION != 1
#error "version mismatch between capnp_c.h and generated code"
#endif

#ifndef capnp_nowarn
# ifdef __GNUC__
#  define capnp_nowarn __extension__
# else
#  define capnp_nowarn
# endif
#endif


#ifdef __cplusplus
extern "C" {
#endif

struct Config;

typedef struct {capn_ptr p;} Config_ptr;

typedef struct {capn_ptr p;} Config_list;

struct Config {
	int32_t timeout;
};

static const size_t Config_word_count = 1;

static const size_t Config_pointer_count = 0;

static const size_t Config_struct_bytes_count = 8;


Config_ptr new_Config(struct capn_segment*);

Config_list new_Config_list(struct capn_segment*, int len);

void read_Config(struct Config*, Config_ptr);

void write_Config(const struct Config*, Config_ptr);

void get_Config(struct Config*, Config_list, int i);

void set_Config(const struct Config*, Config_list, int i);

#ifdef __cplusplus
}
#endif
#endif
