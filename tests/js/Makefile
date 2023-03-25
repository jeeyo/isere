QJSC := qjsc
XXD := xxd
CC := gcc

TEST_JS_FILES += $(wildcard *.js)
TEST_DLL_FILES += $(TEST_JS_FILES:%.js=%.so)

all: ${TEST_DLL_FILES}

%.so: %.js
	$(XXD) -n handler -i $< $@.c
	$(CC) -I../include -shared -fPIC -o $@ $@.c

clean:
	rm *.c
	rm *.so
