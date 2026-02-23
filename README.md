# Microdlna

This is a fork of microdlna which is itself a fork a minidlna.

Microdlna is a stateless dlna server with no external dependencies with an emphasis on
low memory usage.

It has no database, search capabilities, sorting, thumbnails, or format conversion,
but it can cope with the media directory not existing at start time.

## Modifications

There are many security fix and a major fix on large file sending.

Add raspberry pi config file

Add systemd and install/uninstall scripts

## Compile

    ./configure.sh
    make

### Compile Release

Compile with more optimisations and stripping

    ./configure.sh
    make BUILD=release

## Installation

Install systemd service (cf config file in /usr/local/etc/microdlna.conf)

    ./install.sh


Uninstall systemd service

    ./uninstall.sh

## Thanks

Many thanks to Michael J Walsh for his projet https://github.com/mjfwalsh/microdlna which avoid me a lot of work