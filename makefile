CFLAGS = -Wall -O2 -Wextra -Werror -fno-strict-aliasing
LIBS = -lrt
INSTDIR ?= /usr


MANDIR=/usr/share/man
MANSDIR=${MANDIR}/man8


ctl : ctl.c
	$(CXX) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LIBS)

.PHONY: clean
clean:
	rm -rf *.o ctl

.PHONY: install
install:
	mkdir -p $(INSTDIR)/sbin
	cp ctl $(INSTDIR)/sbin
	@if test "${PLATFORM}" = "solaris"; then \
		d=${MANDIR}/man1m; \
		t=ctl.1m; \
	else \
		d=${MANDIR}/man8; \
		t=ctl.8; \
	fi; set -x; mkdir -p $$d \
	  && cp ctl.8 $$d/$$t

