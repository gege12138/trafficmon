#!/bin/sh
set -eu
if [ "$#" -ne 1 ] || [ ! -d "$1/package" ]; then
	echo "usage: $0 /path/to/sdk" >&2
	exit 2
fi
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
destination="$1/package/luci-app-trafficmon"
rm -rf -- "$destination"
mkdir -p "$destination"
cp "$script_dir/Makefile" "$destination/Makefile"
cp -R "$script_dir/root" "$script_dir/htdocs" "$destination/"
echo "staged luci-app-trafficmon at $destination"
