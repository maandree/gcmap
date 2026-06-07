.POSIX:

CONFIGFILE = config.mk
include $(CONFIGFILE)

OBJ =\
	gcmap.o\
	char-table.o\
	colour.o

HDR =\
	common.h\
	char-table.h

all: gcmap
$(OBJ): $(HDR)

.c.o:
	$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS) $(CFLAGS_GTK2)

gcmap: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS) $(LDFLAGS_GTK2)

install: gcmap
	mkdir -p -- "$(DESTDIR)$(PREFIX)/bin"
	mkdir -p -- "$(DESTDIR)$(MANPREFIX)/man1/"
	cp -- gcmap "$(DESTDIR)$(PREFIX)/bin/"
	cp -- gcmap.1 "$(DESTDIR)$(MANPREFIX)/man1/"

uninstall:
	-rm -f -- "$(DESTDIR)$(PREFIX)/bin/gcmap"
	-rm -f -- "$(DESTDIR)$(MANPREFIX)/man1/gcmap.1"

clean:
	-rm -f -- *.o *.a *.lo *.su *.so *.so.* *.gch *.gcov *.gcno *.gcda
	-rm -f -- gcmap

.SUFFIXES:
.SUFFIXES: .o .c

.PHONY: all install uninstall clean
