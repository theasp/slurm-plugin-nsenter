PACKAGE    ?= slurm-plugin-setns

sysconfdir ?= /etc/slurm-llnl

LIBNAME    ?= lib$(shell uname -m | grep -q x86_64 && echo 64)
# TODO: Don't hardcode libdir
LIBDIR     ?= /usr/lib/x86_64-linux-gnu
BINDIR     ?= /usr/bin
SBINDIR    ?= /sbin
LIBEXECDIR ?= /usr/libexec
PLUGINDIR  ?= $(LIBDIR)/slurm-wlm
PLUGSTACKDIR ?= $(sysconfdir)/plugstack.d

export LIBNAME LIBDIR BINDIR SBINDIR LIBEXECDIR PACKAGE

CFLAGS   = -Wall -ggdb

PLUGINS = \
   task_setns.so

LIBRARIES =
SUBDIRS =

all: $(PLUGINS) $(LIBRARIES) subdirs

.SUFFIXES: .c .o .so

.c.o:
	$(CC) $(CFLAGS) -o $@ -fPIC -c $<
.o.so:
	$(CC) -shared -o $*.so $< $(LIBS)

subdirs:
	@for d in $(SUBDIRS); do make -C $$d; done

clean: subdirs-clean
	rm -f *.so *.o lib/*.o

install:
	@mkdir -p --mode=0755 $(DESTDIR)$(PLUGINDIR)
	@for p in $(PLUGINS); do \
	   echo "Installing $$p in $(PLUGINDIR)"; \
	   install -m0755 $$p $(DESTDIR)$(PLUGINDIR); \
	 done
	@for f in $(LIBRARIES); do \
	   echo "Installing $$f in $(PLUGINDIR)"; \
	   install -m0755 $$f $(DESTDIR)$(PLUGINDIR); \
	 done
	@for d in $(SUBDIRS); do \
	   make -C $$d DESTDIR=$(DESTDIR) install; \
	 done
	@mkdir -p --mode=0755 $(DESTDIR)$(PLUGSTACKDIR)
	@install -m0644 plugstack.conf $(DESTDIR)$(PLUGINDIR)/setns.conf;

subdirs-clean:
	@for d in $(SUBDIRS); do make -C $$d clean; done
