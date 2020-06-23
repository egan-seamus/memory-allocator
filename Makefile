

all:
	(cd tests && make)

clean:
	(cd tests && make clean)
	rm -f valgrind.out stdout.txt stderr.txt *.plist

test: hmalloc.c
	gcc -g -o testbuild hmalloc.c


