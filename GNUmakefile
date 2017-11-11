# For multiple programs using a single source file each,
# we can just define 'progs' and create custom targets.
PROGS	=	netmap-txrx
LIBNETMAP =

CLEANFILES = $(PROGS) *.o

SRCDIR ?= ../..

NO_MAN=
CFLAGS = -O2 -pipe
CFLAGS += -Werror -Wall -Wunused-function
CFLAGS += -I $(SRCDIR)/sys -I $(SRCDIR)/apps/include
CFLAGS += -Wextra

LDLIBS += -lpthread
ifeq ($(shell uname),Linux)
	LDLIBS += -lrt	# on linux
endif

PREFIX ?= /usr/local

all: $(PROGS)

clean:
	-@rm -rf $(CLEANFILES)
