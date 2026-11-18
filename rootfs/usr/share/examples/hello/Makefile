LD  = ld
LIB = /usr/lib

hello: hello.o
	$(LD) -o hello $(LIB)/crt0.o hello.o $(LIB)/libc.a

clean:
	rm -f hello
