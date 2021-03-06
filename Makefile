# change CROSS_COMPILE line to your own path
CROSS_COMPILE=/opt/toolchains/uclibc-crosstools-gcc-4.4.2-1/usr/bin/mips-linux-

CC=      $(CROSS_COMPILE)gcc
AR=      $(CROSS_COMPILE)ar
LD=      $(CROSS_COMPILE)ld
NM=      $(CROSS_COMPILE)nm
STRIP=   $(CROSS_COMPILE)strip
RANLIB=  $(CROSS_COMPILE)ranlib
OBJDUMP= $(CROSS_COMPILE)objdump

SBINDIR=	$(DESTDIR)/sbin
USRSBINDIR=	$(DESTDIR)/usr/sbin
VARPATH=	$(DESTDIR)/var
LOGDIR=		$(DESTDIR)/var/log

PROGS= login

LDFLAGS=	-lcrypt -L ../lib
CFLAGS=		-pipe -O2 -fomit-frame-pointer -Wall \
		-DSBINDIR=\"$(SBINDIR)\" \
		-DUSRSBINDIR=\"$(USRSBINDIR)\" \
		-DLOGDIR=\"$(LOGDIR)\" \
		-DVARPATH=\"$(VARPATH)\"

OBJS = login.o

all: $(PROGS)

.c.o:
	$(CC) -c $(CFLAGS) $<

$(PROGS): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	$(STRIP) -s $@ 

.PHONY: install
install: $(PROGS)

.PHONY: clean
clean:
	rm -f $(PROGS) *.o core
