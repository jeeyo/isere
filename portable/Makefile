CC := gcc

all: portable.so

portable.so:
	$(CC) -I./include -I../include -I../3rdparty/FreeRTOS/include -I../3rdparty/FreeRTOS/portable/ThirdParty/GCC/Posix -I../3rdparty/FreeRTOS/portable/ThirdParty/GCC/Posix/utils -shared -fPIC -o $@ src/*.c

clean:
	rm *.so
