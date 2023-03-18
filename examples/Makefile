QJSC := qjsc
XXD := xxd
CC := gcc

all: echo.esm.so

%.so: %.js
	$(XXD) -n handler -i $< $@.c
	$(CC) -I../include -shared -fPIC -o $@ $@.c

clean:
	rm *.c
	rm *.so
