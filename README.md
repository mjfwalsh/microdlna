# Microdlna

This is a fork of microdlna which is itself a fork a minidlna.

Microdlna is a stateless dlna server with no external dependencies with an emphasis on
low memory usage.

It has no database, search capabilities, sorting, thumbnails, or format conversion,
but it can cope with the media directory not existing at start time.

## Compile

    ./configure.sh
    make

## Installation

### Systems with systemd

Install systemd service and config file in /usr/local/etc/microdlna.conf

    ./install.sh

Uninstall systemd service

    ./uninstall.sh

### Other systems

    make install

    make uninstall

## Testing

    make test

Requires python and lldb. Be sure not to use mismatched python and lldb versions.
