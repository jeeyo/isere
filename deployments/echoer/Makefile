CC := gcc

all: echoer.so

%.so: %.c
	$(CC) -I../../include -shared -fPIC -o $@ $<

clean:
	rm *.so
