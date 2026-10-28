#!/bin/sh
set -eu

WENKAI_URL=https://github.com/lxgw/LxgwWenKai-Screen/releases/download/v1.530/LXGWWenKaiGBScreen.ttf
WENKAI_SHA256=0df4009ac9dce6eb3b8cb4c4e90de67bb9745e8992a255c03a58dd5fe3f1e503
INTER_URL=https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
INTER_SHA256=78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3

font_dir=${1:-$(dirname "$0")/../fonts}
mkdir -p "$font_dir"
download_dir=$(mktemp -d "$font_dir/.download.XXXXXX")
trap 'rm -rf "$download_dir"' EXIT

install_font() {
    name=$1
    expected=$2
    actual=$(sha256sum "$download_dir/$name" | cut -d ' ' -f 1)
    if [ "$actual" != "$expected" ]; then
        echo "${0##*/}: $name has SHA-256 $actual, expected $expected" >&2
        exit 1
    fi
    mv "$download_dir/$name" "$font_dir/$name"
}

curl -fsSL -o "$download_dir/LXGWWenKaiGBScreen.ttf" "$WENKAI_URL"
install_font LXGWWenKaiGBScreen.ttf "$WENKAI_SHA256"

curl -fsSL -o "$download_dir/Inter-4.1.zip" "$INTER_URL"
unzip -p "$download_dir/Inter-4.1.zip" extras/ttf/Inter-SemiBold.ttf >"$download_dir/Inter-SemiBold.ttf"
install_font Inter-SemiBold.ttf "$INTER_SHA256"
