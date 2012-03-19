
.PHONY = all molt doc install uninstall clean

WARNINGS := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
			-Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
			-Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
			-Wuninitialized -Wconversion -Wstrict-prototypes
CFLAGS := -g -std=c99 $(WARNINGS)

PROGRAMS = molt
DOCS = molt.1.gz

SRCFILES =	main.c actions.c rules.c variables

HDRFILES =	main.h molt.h internal.h rules.h variables.h

OBJFILES =	main.o actions.o rules.o variables.o

MANFILES = molt.1

all: $(PROGRAMS) $(DOCS)

molt: $(OBJFILES)
	$(CC) -o molt $(OBJFILES) `pkg-config --libs glib-2.0 gmodule-2.0`

main.o:	main.c main.h molt.h internal.h rules.h variables.h
	$(CC) -c $(CFLAGS) `pkg-config --cflags glib-2.0 gmodule-2.0` main.c

actions.o: actions.c molt.h internal.h
	$(CC) -c $(CFLAGS) `pkg-config --cflags glib-2.0` actions.c

rules.o: rules.c rules.h internal.h
	$(CC) -c $(CFLAGS) `pkg-config --cflags glib-2.0` rules.c

variables.o: variables.c variables.h molt.h
	$(CC) -c $(CFLAGS) `pkg-config --cflags glib-2.0` variables.c

doc: $(DOCS)

molt.1.gz: $(MANFILES)
	gzip -c molt.1 > molt.1.gz


install:
	install -Ds -m755 molt $(DESTDIR)/usr/bin/molt
	install -D -m644 molt.1.gz $(DESTDIR)/usr/share/man/man1/molt.1.gz
	mkdir $(DESTDIR)/usr/lib/molt

uninstall:
	rm -f $(DESTDIR)/usr/bin/molt
	rm -f $(DESTDIR)/usr/share/man/man1/molt.1.gz
	rmdir $(DESTDIR)/usr/lib/molt

clean:
	rm -f $(PROGRAMS)
	rm -f $(OBJFILES)
	rm -f $(DOCS)
