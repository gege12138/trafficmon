#!/bin/sh
set -eu

if [ "$#" -ne 1 ] || [ ! -d "$1/package" ]; then
	echo "usage: $0 /path/to/openwrt-sdk" >&2
	exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
source_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
destination="$1/package/trafficmon"

rm -rf -- "$destination"
mkdir -p "$destination/source"
cp "$script_dir/trafficmon/Makefile" "$destination/Makefile"
cp -R "$script_dir/trafficmon/files" "$destination/files"
cp "$source_dir/CMakeLists.txt" "$source_dir/LICENSE" "$destination/source/"
cp -R "$source_dir/src" "$source_dir/cli" "$destination/source/"
echo "staged trafficmon package at $destination"
