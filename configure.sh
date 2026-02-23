#!/bin/sh

tab=$(printf '\t')
objs=$(ls -1 *.c | grep -v version | sed -e 's|\.c$|.o|' | tr '\n' ' ')

exec 1> Makefile

cat <<MAKEFILE
# Build type: dev (default) | release
#   make release  -> -O3, LTO, -ffunction-sections -fdata-sections, --gc-sections, sans -g
#   make           -> -O2, -g (débogage)
BUILD ?= dev

BASE_CFLAGS := --std=c11 -D_GNU_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64
CFLAGS_DEV := \$(BASE_CFLAGS) -g -O2
CFLAGS_RELEASE := \$(BASE_CFLAGS) -O3 -flto -ffunction-sections -fdata-sections
LDFLAGS_DEV := -pthread
LDFLAGS_RELEASE := -pthread -flto -Wl,--gc-sections

ifeq (\$(BUILD),release)
  CFLAGS := \$(CFLAGS_RELEASE)
  LDFLAGS := \$(LDFLAGS_RELEASE)
else
  CFLAGS := \$(CFLAGS_DEV)
  LDFLAGS := \$(LDFLAGS_DEV)
endif

DST := $objs version.c

.PHONY: all
all: microdlnad microdlnad.8

MAKEFILE

# run the compiler to find dependancies
printf 'deps:\n\t@$(CC) -MM -MG -MF - -c *.c\n' | make -f - \
    | sed -e 's|version\.o|microdlnad|' | sed -e 's|version_info\.h|gen_version.sh|'

cat <<MAKEFILE

%.o: %.c
${tab}\$(CC) -c \$(CFLAGS) \$< -o \$@

microdlnad: \$(DST)
${tab}./gen_version.sh > version_info.h
${tab}\$(CC) \$(CFLAGS) -o microdlnad \$(DST) \$(LDFLAGS)

microdlnad.8: microdlna.pod
${tab}pod2man -c multimedia -r '' microdlna.pod > microdlnad.8

.PHONY: clean
clean:
${tab}rm -f *.o microdlnad microdlnad.8 version_info.h

.PHONY: install
install:
${tab}mkdir -p /usr/local/bin
${tab}cp microdlnad /usr/local/bin
${tab}strip /usr/local/bin/microdlnad
${tab}mkdir -p /usr/local/share/man/man8
${tab}cp microdlnad.8 /usr/local/share/man/man8

MAKEFILE
