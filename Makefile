CFLAGS := -std=gnu17 -Wall -D_GNU_SOURCE $(CFLAGS)

DEPS=catsock.o socks.o utils.o forwarder_darwin.o forwarder_linux.o

catsock: $(DEPS) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o catsock $(DEPS)

%.o: %.c *.h Makefile
	$(CC) $(CFLAGS) -c $< -o $@

format:
	clang-format -i *.c *.h

clean:
	rm -f *.o catsock

cleanartefacts:
	rm -f catsock-*

.PHONY: clean cleanartefacts format
