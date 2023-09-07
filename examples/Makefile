QJSC := qjsc
XXD := xxd
CC := gcc

all: echo.esm.so

%.so: %.js
	$(QJSC) -c -N handler -m -o $@.c $<
	$(CC) -I../include -shared -fPIC -o $@ $@.c

clean:
	rm *.c
	rm *.so
