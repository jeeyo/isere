#include "config.capnp.h"
/* AUTO GENERATED - DO NOT EDIT */
#ifdef __GNUC__
# define capnp_unused __attribute__((unused))
# define capnp_use(x) (void) (x);
#else
# define capnp_unused
# define capnp_use(x)
#endif


Config_ptr new_Config(struct capn_segment *s) {
	Config_ptr p;
	p.p = capn_new_struct(s, 8, 0);
	return p;
}
Config_list new_Config_list(struct capn_segment *s, int len) {
	Config_list p;
	p.p = capn_new_list(s, len, 8, 0);
	return p;
}
void read_Config(struct Config *s capnp_unused, Config_ptr p) {
	capn_resolve(&p.p);
	capnp_use(s);
	s->timeout = (int32_t) ((int32_t)capn_read32(p.p, 0));
}
void write_Config(const struct Config *s capnp_unused, Config_ptr p) {
	capn_resolve(&p.p);
	capnp_use(s);
	capn_write32(p.p, 0, (uint32_t) (s->timeout));
}
void get_Config(struct Config *s, Config_list l, int i) {
	Config_ptr p;
	p.p = capn_getp(l.p, i, 0);
	read_Config(s, p);
}
void set_Config(const struct Config *s, Config_list l, int i) {
	Config_ptr p;
	p.p = capn_getp(l.p, i, 0);
	write_Config(s, p);
}
