CFLAGS=-O2 -Wall -Wno-unused-parameter -Wextra -Wwrite-strings

ALL=glycerin

all: $(ALL)

glycerin: glycerin.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $<

debug:
	$(MAKE) all CFLAGS="$(CFLAGS) -g -Og -DDEBUG -D_FORTIFY_SOURCE=2"

clean:
	rm $(ALL)
