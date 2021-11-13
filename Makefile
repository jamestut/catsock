CFLAGS=-O0 -g3 -Wall -D_GNU_SOURCE

DEPS=catsock.o socks.o utils.o forwarder_darwin.o forwarder_linux.o

catsock: $(DEPS) Makefile
	$(CC) $(CFLAGS) -o catsock $(DEPS)

%.o: %.c *.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@

format:
	clang-format -i *.c *.h

clean:
	rm -f *.o catsock

.PHONY: clean format
