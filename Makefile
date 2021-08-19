# See LICENSE for license details.
include config.mk

OBOERU_SRC = oboeru.c util.c
OBOERU_OBJ = $(OBOERU_SRC:.c=.o)

BINS = oboeru oboerudata oboeruhttp

default: $(BINS)

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

install: $(BINS)
	mkdir -p $(PREFIX)/bin
	cp $(BINS) $(PREFIX)/bin
	chmod 755 $(BINS:%=$(PREFIX)/bin/%)

uninstall:
	rm $(BINS:%=$(PREFIX)/bin/%)

clean:
	rm *.o $(BINS)
