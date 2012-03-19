
.PHONY = all magicvar.so install uninstall clean

WARNINGS := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
			-Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
			-Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
			-Wuninitialized -Wconversion -Wstrict-prototypes
CFLAGS := -g -std=c99 $(WARNINGS)

PROGRAMS = magicvar.so

SRCFILES =	main.c

HDRFILES =	

OBJFILES =	main.o

all: $(PROGRAMS)

magicvar.so: $(OBJFILES)
	$(CC) -shared -Wl,-soname,libmagicvar.so.1 -o magicvar.so \
		$(OBJFILES) `pkg-config --libs glib-2.0`

main.o:	main.c
	$(CC) -c -fPIC $(CFLAGS) -I../../src `pkg-config --cflags glib-2.0` main.c

install:
	install -Ds -m755 magicvar.so $(DESTDIR)/usr/lib/molt/magicvar.so

uninstall:
	rm -f $(DESTDIR)/usr/lib/molt/magicvar.so

clean:
	rm -f $(PROGRAMS)
	rm -f $(OBJFILES)
