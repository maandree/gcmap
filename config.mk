PREFIX    = /usr
MANPREFIX = $(PREFIX)/share/man

CC = c99

CPPFLAGS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=700 -D_GNU_SOURCE
CFLAGS   = $$(pkg-config --cflags pango)
LDFLAGS  = -lsimple -lcmap $$(pkg-config --libs pango)

CFLAGS_GTK2  = $$(pkg-config --cflags gtk+-2.0)
LDFLAGS_GTK2 = $$(pkg-config --libs gtk+-2.0)
