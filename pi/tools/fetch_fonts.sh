#!/bin/sh
set -eu

WENKAI_URL=https://github.com/lxgw/LxgwWenKai-Screen/releases/download/v1.530/LXGWWenKaiGBScreen.ttf
WENKAI_SHA256=0df4009ac9dce6eb3b8cb4c4e90de67bb9745e8992a255c03a58dd5fe3f1e503
INTER_ZIP_URL=https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
INTER_ZIP_SHA256=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
INTER_SHA256=78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3

font_dir=${1:-$(dirname "$0")/../fonts}
mkdir -p "$font_dir"
download_dir=$(mktemp -d "$font_dir/.download.XXXXXX")
trap 'rm -rf "$download_dir"' EXIT

sha256_of() {
    sha256sum "$1" | cut -d ' ' -f 1
}

check_sha256() {
    actual=$(sha256_of "$download_dir/$1")
    if [ "$actual" != "$2" ]; then
        echo "${0##*/}: $1 has SHA-256 $actual, expected $2" >&2
        exit 1
    fi
}

in_place() {
    [ -f "$font_dir/$1" ] && [ "$(sha256_of "$font_dir/$1")" = "$2" ]
}

install_font() {
    check_sha256 "$1" "$2"
    mv "$download_dir/$1" "$font_dir/$1"
}

if ! in_place LXGWWenKaiGBScreen.ttf "$WENKAI_SHA256"; then
    curl -fsSL -o "$download_dir/LXGWWenKaiGBScreen.ttf" "$WENKAI_URL"
    install_font LXGWWenKaiGBScreen.ttf "$WENKAI_SHA256"
fi

if ! in_place Inter-SemiBold.ttf "$INTER_SHA256"; then
    curl -fsSL -o "$download_dir/Inter-4.1.zip" "$INTER_ZIP_URL"
    check_sha256 Inter-4.1.zip "$INTER_ZIP_SHA256"
    unzip -p "$download_dir/Inter-4.1.zip" extras/ttf/Inter-SemiBold.ttf >"$download_dir/Inter-SemiBold.ttf"
    install_font Inter-SemiBold.ttf "$INTER_SHA256"
fi
