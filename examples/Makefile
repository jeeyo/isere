QJSC := qjsc
XXD := xxd
CC := gcc

all: echo.esm.so

%.so: %.js
	# echo "let exports = { handler: () => {} }" > $<.tmp
	# cat $< >> $<.tmp
	# echo ")" >> $<.tmp
	# echo "exports.handler()" >> $<.tmp
	# $(QJSC) -N handler -fno-bigint -fno-eval -c -o $@.c $<
	# $(XXD) -n handler -i $<.tmp $@.c
	$(XXD) -n handler -i $< $@.c
	$(CC) -I../include -shared -fPIC -o $@ $@.c
	# rm $<.tmp

clean:
	rm *.c
	rm *.so
