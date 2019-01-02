include $(ROOTDIR)/config.arch

BOGUS_TARGETS		:= FORCE makefile $(ROOTDIR)/config.arch

CC			:= $(CROSS_COMPILE)gcc
STRIP			:= $(CROSS_COMPILE)strip
CFLAGS			:= -Os
#LDFLAGS			:= -L$(ROOTDIR)/lib/ncurses/lib -lncurses

export CC CFLAGS

#
# Settings that are specific to this package
#

CPPFLAGS		:= -I$(ROOTDIR)/lib/ncurses/include
LDFLAGS			:= -L$(ROOTDIR)/lib/ncurses/lib -lncurses

.PHONY: all
all::
	if [ ! -e Makefile ]; then \
		./configure --host=$(MACHINE)-linux \
			CPPFLAGS="$(CPPFLAGS)" LDFLAGS="$(LDFLAGS)" \
			--disable-nls --disable-color || exit 1 ; \
	fi
	$(MAKE) -f Makefile

.PHONY: clean distclean
clean distclean::
	[ ! -f Makefile ] || $(MAKE) -f Makefile $@

.PHONY: romfs
romfs::
	$(STRIP) src/nano
	$(ROMFSINST) -e CONFIG_USER_NANO_NANO src/nano /bin/nano

# These targets must not be passed through to the original Makefile
.PHONY: $(BOGUS_TARGETS)
$(BOGUS_TARGETS)::

# Everything else (maybe including clean, distclean) does get passed through
%:: FORCE
	$(MAKE) -f Makefile $@
