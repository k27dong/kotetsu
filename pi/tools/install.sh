#!/bin/bash
set -euo pipefail

umask 022 # root's umask may be stricter, and the kotetsu user has to read what is installed here
export DEBIAN_FRONTEND=noninteractive

CONFIG_TXT=/boot/firmware/config.txt
SPI_DEVICE=/dev/spidev0.0

fail_usage() {
    echo "${0##*/}: $1" >&2
    echo "usage: ${0##*/} [--dry-run] --pi-key-file <path>" >&2
    exit 2
}

die() {
    echo "${0##*/}: $*" >&2
    exit 1
}

step() {
    echo "[$1/8] $2"
}

run() {
    echo "    $*"
    [ "$dry_run" = 1 ] || "$@"
}

append_to_config_txt() {
    # without a final newline the new line would join the last one
    [ -z "$(tail -c 1 "$CONFIG_TXT")" ] || echo >>"$CONFIG_TXT"
    # in a model section such as [cm5] the lines would apply to that model only
    awk 'BEGIN { all = 1 } /^\[/ { all = ($0 == "[all]") } END { exit !all }' "$CONFIG_TXT" ||
        echo "[all]" >>"$CONFIG_TXT"
    printf '%s\n' "$@" >>"$CONFIG_TXT"
}

dry_run=0
pi_key_file=
while [ $# -gt 0 ]; do
    case $1 in
    --dry-run)
        dry_run=1
        ;;
    --pi-key-file)
        [ $# -ge 2 ] || fail_usage "--pi-key-file needs a path"
        pi_key_file=$2
        shift
        ;;
    *)
        fail_usage "unknown argument $1"
        ;;
    esac
    shift
done
[ -n "$pi_key_file" ] || fail_usage "--pi-key-file is required"

if [ "$dry_run" = 0 ]; then
    [ "$(id -u)" -eq 0 ] || die "run as root, or pass --dry-run"
    [ -s "$pi_key_file" ] || die "$pi_key_file is missing or empty"
    [ -f "$CONFIG_TXT" ] || die "$CONFIG_TXT not found, so this isn't Raspberry Pi OS"
fi

repo_root=$(cd "$(dirname "$0")/../.." && pwd)
reboot_needed=0

if [ "$dry_run" = 1 ]; then
    echo "dry run: the commands below are listed, not run"
fi

step 1 "Install packages with apt"
run apt-get update
run apt-get install -y -o Dpkg::Options::=--force-confold build-essential pkgconf \
    libpango1.0-dev libcairo2-dev libcjson-dev libcurl4-openssl-dev libgpiod-dev clang-format \
    comitup curl unzip

step 2 "Create the kotetsu user in gpio and spi"
if ! id -u kotetsu >/dev/null 2>&1; then
    run useradd --system --user-group --no-create-home --home-dir /var/lib/kotetsu \
        --shell /usr/sbin/nologin kotetsu
fi
run usermod --append --groups gpio,spi kotetsu

step 3 "Set kernel=kernel8.img and dtparam=spi=on in config.txt"
missing_lines=()
for line in kernel=kernel8.img dtparam=spi=on; do
    if grep -qxF "$line" "$CONFIG_TXT" 2>/dev/null; then
        echo "    $line is already set"
    else
        missing_lines+=("$line")
    fi
done
if [ ${#missing_lines[@]} -gt 0 ]; then
    reboot_needed=1
    run append_to_config_txt "${missing_lines[@]}"
fi

step 4 "Download the fonts"
run "$repo_root/pi/tools/fetch_fonts.sh" /usr/local/share/kotetsu/fonts

step 5 "Build the release binary"
if [ -n "${SUDO_USER:-}" ]; then
    # so pi/build-release/ isn't root-owned in the clone
    run runuser -u "$SUDO_USER" -- make -C "$repo_root/pi" RELEASE=1
else
    run make -C "$repo_root/pi" RELEASE=1
fi

step 6 "Install the program, service, Wi-Fi callback and Comitup config"
run make -C "$repo_root/pi" RELEASE=1 install PREFIX=/usr/local
run install -m 644 "$repo_root/pi/systemd/kotetsu.service" /etc/systemd/system/kotetsu.service
run install -d /usr/local/lib/kotetsu
run install -m 755 -o kotetsu -g kotetsu "$repo_root/pi/comitup/wifi-state-callback" \
    /usr/local/lib/kotetsu/wifi-state-callback
run install -m 644 "$repo_root/pi/comitup/comitup.conf" /etc/comitup.conf
run systemctl daemon-reload

step 7 "Install the Pi key"
run install -d -m 700 /etc/kotetsu
if [ "$pi_key_file" -ef /etc/kotetsu/pi-key ]; then
    echo "    $pi_key_file is already installed"
else
    run install -m 600 -o root -g root "$pi_key_file" /etc/kotetsu/pi-key
fi

step 8 "Enable the service"
run systemctl enable kotetsu.service
if [ -e "$SPI_DEVICE" ]; then
    run systemctl restart kotetsu.service
    # a program that can't start (no key, no SPI, no screen) exits within a moment
    run sleep 2
    run systemctl is-active --quiet kotetsu.service ||
        die "kotetsu.service isn't running, see journalctl -u kotetsu"
else
    echo "    $SPI_DEVICE isn't there yet, so the service starts after the reboot"
    reboot_needed=1
fi

if [ "$dry_run" = 1 ]; then
    echo "dry run: nothing was changed"
elif [ "$reboot_needed" = 1 ]; then
    echo "done: reboot to apply the config.txt changes and start kotetsu"
else
    echo "done"
fi
