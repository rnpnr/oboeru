# See LICENSE for license details.
include config.mk

OBOERU_SRC = oboeru.c util.c
OBOERU_OBJ = $(OBOERU_SRC:.c=.o)

default: oboeru oboerudata oboeruhttp

config.h:
	cp config.def.h $@

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

$(OBOERU_OBJ): config.h

oboeru: $(OBOERU_OBJ)
	$(CC) -o $@ $(OBOERU_OBJ) $(LDFLAGS)

oboerudata: oboerudata.go
	go build -ldflags "$(GOLDFLAGS)" $@.go
oboeruhttp: oboeruhttp.go
	go build -ldflags "$(GOLDFLAGS)" $@.go

install: oboeru oboerudata oboeruhttp
	mkdir -p $(PREFIX)/bin
	cp oboeru $(PREFIX)/bin
	cp oboerudata $(PREFIX)/bin
	cp oboeruhttp $(PREFIX)/bin
	chmod 755 $(PREFIX)/bin/oboeru
	chmod 755 $(PREFIX)/bin/oboerudata
	chmod 755 $(PREFIX)/bin/oboeruhttp

uninstall:
	rm $(PREFIX)/bin/oboeru
	rm $(PREFIX)/bin/oboerudata
	rm $(PREFIX)/bin/oboeruhttp

clean:
	rm *.o oboeru oboerudata oboeruhttp
