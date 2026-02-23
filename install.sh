#!/bin/bash
# MicroDLNA installation script
# Copies binary and config to /usr/local, installs systemd unit and starts the service.
# Must be run as root (e.g. sudo ./install.sh).

set -e

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
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

if [ ! -f "${SELF_DIR}/${BINARY_NAME}" ]; then
    echo "Binary ${BINARY_NAME} not found in ${SELF_DIR}. Run 'make' first." >&2
    exit 1
fi

echo "Installing MicroDLNA..."

mkdir -p "$BIN_DIR"
install -m 755 "${SELF_DIR}/${BINARY_NAME}" "$BIN_DIR/${BINARY_NAME}"
strip "$BIN_DIR/${BINARY_NAME}" 2>/dev/null || true
echo "  -> $BIN_DIR/${BINARY_NAME}"

mkdir -p "$ETC_DIR"
if [ ! -f "$ETC_DIR/$CONFIG_NAME" ]; then
    install -m 644 "${SELF_DIR}/${CONFIG_NAME}" "$ETC_DIR/$CONFIG_NAME"
    echo "  -> $ETC_DIR/$CONFIG_NAME (new)"
else
    echo "  -> $ETC_DIR/$CONFIG_NAME (already exists, not overwritten)"
fi

if [ ! -f "${SELF_DIR}/${SERVICE_NAME}" ]; then
    echo "Service file ${SERVICE_NAME} not found in ${SELF_DIR}." >&2
    exit 1
fi
install -m 644 "${SELF_DIR}/${SERVICE_NAME}" "$SYSTEMD_DIR/${SERVICE_NAME}"
echo "  -> $SYSTEMD_DIR/${SERVICE_NAME}"

# Man page (built by Makefile from microdlna.pod)
if [ ! -f "${SELF_DIR}/${MAN_NAME}" ]; then
    echo "Man page ${MAN_NAME} not found in ${SELF_DIR}. Run 'make' first." >&2
    exit 1
fi
mkdir -p "$MAN_DIR"
install -m 644 "${SELF_DIR}/${MAN_NAME}" "$MAN_DIR/${MAN_NAME}"
echo "  -> $MAN_DIR/${MAN_NAME}"

systemctl daemon-reload
systemctl enable "$SERVICE_NAME"
systemctl start "$SERVICE_NAME"

echo ""
echo "MicroDLNA has been installed and started."
echo "  Config: $ETC_DIR/$CONFIG_NAME  (ensure media_dir is set)"
echo "  Status: systemctl status $SERVICE_NAME"
echo "  Logs:   journalctl -u $SERVICE_NAME -f"
