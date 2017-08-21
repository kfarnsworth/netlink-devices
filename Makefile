#
# Makefile for netlink test
#

SYSROOTFS?=
includedir=/usr/include
libdir=/usr/lib
bindir=/usr/bin

CC?=gcc
CCLD?=$(CC)
CFLAGS=-g -Wall -I$(SYSROOTFS)$(includedir) -I$(SYSROOTFS)$(includedir)/libnl3
LDFLAGS=-L$(SYSROOTFS)$(libdir)
NL_LIBS=-lnl-route-3 -lnl-3
SYS_LIBS=-lpthread -lm
DESTDIR?=$(PWD)/output

EXE=nltest
SRCS=main.c \
    netlink_devices.c \
    uevent_devices.c

OBJS=${SRCS:.c=.o}

all: build

build: $(EXE)

.c.o :
	$(CC) $(CFLAGS) -c $<

$(EXE): $(OBJS)
	$(CCLD) $(LDFLAGS) $(OBJS) $(NL_LIBS) $(SYS_LIBS) -o $@

clean:
	rm -f $(EXE) *.o

install:
	install -d -m 0755 $(DESTDIR)$(bindir)
	install -m 0755 $(EXE) $(DESTDIR)$(bindir)/.

.PHONY: all build clean install
