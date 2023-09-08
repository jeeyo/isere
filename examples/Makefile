CC := gcc

all: echo.esm.so

%.so: %.js
	xxd -i $< $@.c
	sed -i.bak -E "s/[a-z_]+\[\]/handler\[\]/" $@.c
	sed -i.bak -E "s/[a-z_]+_len/handler_len/" $@.c
	$(CC) -I../include -shared -fPIC -o $@ $@.c

clean:
	rm *.c
	rm *.c.bak
	rm *.so
