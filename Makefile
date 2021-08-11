# See LICENSE for license details.
include config.mk

OBOERU_SRC = oboeru.c util.c
OBOERU_OBJ = $(OBOERU_SRC:.c=.o)

default: oboeru

config.h:
	cp config.def.h $@

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

$(OBOERU_OBJ): config.h

oboeru: $(OBOERU_OBJ)
	$(CC) -o $@ $(OBOERU_OBJ) $(LDFLAGS)

install: oboeru
	mkdir -p $(PREFIX)/bin
	cp oboeru $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/oboeru

uninstall:
	rm $(PREFIX)/bin/oboeru

clean:
	rm *.o oboeru
