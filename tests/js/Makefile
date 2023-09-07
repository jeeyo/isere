QJSC := qjsc
CC := gcc

TEST_JS_FILES += $(wildcard *.js)
TEST_DLL_FILES += $(TEST_JS_FILES:%.js=%.so)

all: ${TEST_DLL_FILES}

%.so: %.js
	$(QJSC) -c -N handler -m -o $@.c $<
	$(CC) -I../include -shared -fPIC -o $@ $@.c

clean:
	rm *.c
	rm *.so
