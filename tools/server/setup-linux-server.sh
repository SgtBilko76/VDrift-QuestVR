#!/bin/bash
# Build the VDrift dedicated server on a Linux box and stage its data.
#
#   ./setup-linux-server.sh /path/to/vdrift /path/to/VDriftVR /path/to/vdrift-data
#
# Installs the build dependencies (Debian/Ubuntu), builds vdrift-server into
# $PREFIX (default ~/vdserver) and points it at the data. The server needs the
# same data tree the game has: tracks, cars, carparts, trackparts. The data
# argument may be the SVN checkout (vdrift-data) or a staged tree.
set -euo pipefail

VD_SRC="${1:?vdrift source tree (branch quest-port)}"
PORT_SRC="${2:?VDriftVR project (for server/CMakeLists.txt)}"
DATA="${3:?VDrift data tree}"
PREFIX="${PREFIX:-$HOME/vdserver}"

if command -v apt-get >/dev/null; then
    sudo apt-get install -y build-essential cmake ninja-build pkg-config \
        libbullet-dev libenet-dev libpng-dev zlib1g-dev libvorbis-dev
fi

mkdir -p "$PREFIX"
cmake -S "$PORT_SRC/server" -B "$PREFIX/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DVD_ROOT="$VD_SRC" -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$PREFIX/build" -j "$(nproc)"
cmake --install "$PREFIX/build"

# The data: a symlink is enough, the server only reads it.
ln -sfn "$(cd "$DATA" && pwd)" "$PREFIX/data"

cat > "$PREFIX/env" <<EOF
export VDRIFT_DATA_DIRECTORY="$PREFIX/data"
export HOME="\${HOME:-$HOME}"
EOF

echo "Server built: $PREFIX/bin/vdrift-server"
echo "Run it with:  $(dirname "$0")/run-server.sh"
