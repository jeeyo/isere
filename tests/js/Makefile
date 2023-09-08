CC := gcc

TEST_JS_FILES += $(wildcard *.js)
TEST_DLL_FILES += $(TEST_JS_FILES:%.js=%.so)

all: ${TEST_DLL_FILES}

%.so: %.js
	xxd -i $< $@.c
	sed -i.bak -E "s/[a-z_]+\[\]/handler\[\]/" $@.c
	sed -i.bak -E "s/[a-z_]+_len/handler_len/" $@.c
	$(CC) -I../include -shared -fPIC -o $@ $@.c

clean:
	rm *.c
	rm *.c.bak
	rm *.so
