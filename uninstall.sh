#!/bin/bash
# MicroDLNA uninstall script
# Stops the service, removes binary, config and systemd unit.
# Must be run as root (e.g. sudo ./uninstall.sh).

BINARY_NAME=microdlnad
CONFIG_NAME=microdlna.conf
SERVICE_NAME=microdlna.service
MAN_NAME=microdlnad.8

BIN_DIR=/usr/local/bin
ETC_DIR=/usr/local/etc
SYSTEMD_DIR=/etc/systemd/system
MAN_DIR=/usr/local/share/man/man8

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (e.g. sudo $0)." >&2
    exit 1
fi

echo "Uninstalling MicroDLNA..."

# Stop and disable service, then remove unit
if systemctl is-active --quiet "$SERVICE_NAME" 2>/dev/null; then
    systemctl stop "$SERVICE_NAME"
    echo "  -> service stopped"
fi
if systemctl is-enabled --quiet "$SERVICE_NAME" 2>/dev/null; then
    systemctl disable "$SERVICE_NAME"
    echo "  -> service disabled"
fi
if [ -f "$SYSTEMD_DIR/${SERVICE_NAME}" ]; then
    rm -f "$SYSTEMD_DIR/${SERVICE_NAME}"
    echo "  -> removed $SYSTEMD_DIR/${SERVICE_NAME}"
fi
systemctl daemon-reload 2>/dev/null || true

# Remove binary
if [ -f "$BIN_DIR/${BINARY_NAME}" ]; then
    rm -f "$BIN_DIR/${BINARY_NAME}"
    echo "  -> removed $BIN_DIR/${BINARY_NAME}"
fi

# Remove config
if [ -f "$ETC_DIR/$CONFIG_NAME" ]; then
    rm -f "$ETC_DIR/$CONFIG_NAME"
    echo "  -> removed $ETC_DIR/$CONFIG_NAME"
fi

# Remove man page
if [ -f "$MAN_DIR/$MAN_NAME" ]; then
    rm -f "$MAN_DIR/$MAN_NAME"
    echo "  -> removed $MAN_DIR/$MAN_NAME"
fi

echo ""
echo "MicroDLNA has been uninstalled."
