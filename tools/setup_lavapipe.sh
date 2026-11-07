#!/usr/bin/env bash
# Reproduce CI's lavapipe Vulkan job without root.
#
# CI installs Ubuntu packages system-wide. This script downloads the same
# packages into an ignored local sysroot, builds the Vulkan target against it,
# and runs the 90-frame Xvfb capture against the committed golden frame.
#
# Usage:
#   tools/setup_lavapipe.sh
#   tools/setup_lavapipe.sh --setup-only
#   CMAKE=/path/to/cmake tools/setup_lavapipe.sh
#
# Optional environment variables:
#   SC_LAVAPIPE_CACHE  package cache and default sysroot (default: .cache/lavapipe)
#   SC_LAVAPIPE_PREFIX extracted sysroot (default: $SC_LAVAPIPE_CACHE/root)
#   SC_LAVAPIPE_BUILD  build directory (default: build-lavapipe)
#   JOBS               build parallelism
#
# The script never writes to system package directories. On a machine without
# /usr/bin/xkbcomp, Xvfb is started in a private user/mount namespace with a
# tiny shim containing the prefixed xkbcomp; that namespace disappears when the
# capture finishes.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cache="${SC_LAVAPIPE_CACHE:-$repo_root/.cache/lavapipe}"
prefix="${SC_LAVAPIPE_PREFIX:-$cache/root}"
build_dir="${SC_LAVAPIPE_BUILD:-$repo_root/build-lavapipe}"
cmake="${CMAKE:-cmake}"
suite="${SC_UBUNTU_SUITE:-noble}"
jobs="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '4')}"

fail() {
    printf 'lavapipe: %s\n' "$*" >&2
    exit 1
}

setup_only=0
if [ "${1:-}" = "--setup-only" ]; then
    setup_only=1
    shift
fi
[ "$#" -eq 0 ] || fail "unknown argument: $*"

command -v apt-get >/dev/null 2>&1 || fail 'apt-get is required'
command -v dpkg-deb >/dev/null 2>&1 || fail 'dpkg-deb is required'
command -v "$cmake" >/dev/null 2>&1 || fail "CMake not found: $cmake (set CMAKE=/path/to/cmake)"
command -v unshare >/dev/null 2>&1 || fail 'unshare is required for the local Xvfb wrapper'
[ "$(uname -s)" = Linux ] || fail 'this script reproduces the Linux CI job only'

mkdir -p "$cache/etc" "$cache/lists" "$cache/cache/archives" "$prefix" "$build_dir"

sources="$cache/etc/sources.list"
cat >"$sources" <<EOF
deb http://archive.ubuntu.com/ubuntu $suite main universe
deb http://archive.ubuntu.com/ubuntu $suite-updates main universe
deb http://security.ubuntu.com/ubuntu $suite-security main universe
EOF

apt_opts=(
    -o "Dir::Etc::sourcelist=$sources"
    -o "Dir::Etc::sourceparts=-"
    -o "Dir::State::lists=$cache/lists"
    -o "Dir::Cache=$cache/cache"
    -o "Dir::Cache::archives=$cache/cache/archives"
    -o "Dir::State::status=/var/lib/dpkg/status"
    -o "APT::Sandbox::User=$USER"
    -o "Debug::NoLocking=1"
)

# The package list mirrors the vulkan-headless job in .github/workflows/ci.yml,
# with x11-xkb-utils, xkb-data and libvulkan1 made explicit: those are runtime
# pieces that a host with an installed Vulkan loader or XKB data may already
# satisfy, so --download-only alone can leave them out of a local sysroot.
packages=(
    libvulkan-dev vulkan-tools vulkan-validationlayers
    mesa-vulkan-drivers xvfb x11-xkb-utils xkb-data libvulkan1
    libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev
    libxfixes-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev
    libdecor-0-dev
)

printf 'lavapipe: downloading Ubuntu %s packages into %s\n' "$suite" "$cache"
apt-get "${apt_opts[@]}" update
apt-get "${apt_opts[@]}" install --download-only --no-install-recommends -y "${packages[@]}"

# Install --download-only treats a package already present on the host as
# satisfied, so fetch the three runtime packages explicitly when needed.
missing=()
for package in libvulkan1 xkb-data x11-xkb-utils; do
    compgen -G "$cache/cache/archives/${package}_*.deb" >/dev/null || missing+=("$package")
done
if [ "${#missing[@]}" -gt 0 ]; then
    (cd "$cache/cache/archives" && apt-get "${apt_opts[@]}" download "${missing[@]}")
fi

shopt -s nullglob
debs=("$cache/cache/archives"/*.deb)
[ "${#debs[@]}" -gt 0 ] || fail "no packages downloaded into $cache/cache/archives"
for deb in "${debs[@]}"; do
    dpkg-deb -x "$deb" "$prefix"
done

[ -f "$prefix/usr/include/vulkan/vulkan.h" ] || fail 'Vulkan headers were not extracted'
[ -e "$prefix/usr/lib/x86_64-linux-gnu/libvulkan.so" ] || fail 'Vulkan loader was not extracted'
[ -f "$prefix/usr/share/vulkan/icd.d/lvp_icd.json" ] || fail 'lavapipe ICD was not extracted'
compgen -G "$prefix/usr/share/vulkan/explicit_layer.d/*khronos_validation*.json" >/dev/null \
    || fail 'the Khronos validation layer was not extracted'
[ -x "$prefix/usr/bin/Xvfb" ] || fail 'Xvfb was not extracted'
[ -x "$prefix/usr/bin/xkbcomp" ] || fail 'xkbcomp was not extracted'

if [ "$setup_only" -eq 1 ]; then
    printf 'lavapipe: setup complete\n'
    exit 0
fi

export PATH="$prefix/usr/bin:$PATH"
export LD_LIBRARY_PATH="$prefix/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export CPATH="$prefix/usr/include${CPATH:+:$CPATH}"
export PKG_CONFIG_PATH="$prefix/usr/lib/x86_64-linux-gnu/pkgconfig:$prefix/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PKG_CONFIG_SYSROOT_DIR="$prefix"
export VK_ICD_FILENAMES="$prefix/usr/share/vulkan/icd.d/lvp_icd.json"
export VK_LAYER_PATH="$prefix/usr/share/vulkan/explicit_layer.d"
export XDG_RUNTIME_DIR="$cache/runtime"
export SDL_VIDEO_DRIVER=x11

mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

printf 'lavapipe: reporting the software device\n'
"$prefix/usr/bin/vulkaninfo" --summary >/dev/null

printf 'lavapipe: configuring and building %s\n' "$build_dir"
"$cmake" -S "$repo_root" -B "$build_dir" \
    -DSC_ENABLE_VULKAN=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$prefix/usr" \
    -DVulkan_INCLUDE_DIR="$prefix/usr/include" \
    -DVulkan_LIBRARY="$prefix/usr/lib/x86_64-linux-gnu/libvulkan.so"
"$cmake" --build "$build_dir" --parallel "$jobs"

xvfb_pid=
shim=
cleanup() {
    if [ -n "$xvfb_pid" ]; then
        kill "$xvfb_pid" 2>/dev/null || true
        wait "$xvfb_pid" 2>/dev/null || true
    fi
    if [ -n "$shim" ]; then
        rm -rf "$shim"
    fi
}
trap cleanup EXIT

xkbdir="$prefix/usr/share/X11/xkb"
[ -d "$xkbdir" ] || xkbdir=/usr/share/X11/xkb

display=99
while [ -S "/tmp/.X11-unix/X$display" ]; do
    display=$((display - 1))
    [ "$display" -ge 90 ] || fail 'no free X display in :90-:99'
done

if [ ! -x /usr/bin/xkbcomp ]; then
    # Xvfb looks for xkbcomp at the compiled-in /usr/bin path. A private
    # namespace lets the prefixed copy take that path without touching /usr/bin.
    shim="$cache/xkb-shim"
    rm -rf "$shim"
    mkdir -p "$shim"
    ln -s "$prefix/usr/bin/xkbcomp" "$shim/xkbcomp"
    cp -L "$(command -v sh)" "$shim/sh"
    chmod +x "$shim/sh"
    unshare -Urnm sh -c '
        set -e
        prefix=$1
        shim=$2
        display=$3
        xkbdir=$4
        export LD_LIBRARY_PATH="$prefix/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu"
        mount --bind "$shim" /usr/bin
        exec "$prefix/usr/bin/Xvfb" ":$display" -ac -screen 0 1920x1080x24 \
            -nolisten tcp -xkbdir "$xkbdir"
    ' sh "$prefix" "$shim" "$display" "$xkbdir" >"$cache/xvfb.log" 2>&1 &
else
    "$prefix/usr/bin/Xvfb" ":$display" -ac -screen 0 1920x1080x24 \
        -nolisten tcp -xkbdir "$xkbdir" >"$cache/xvfb.log" 2>&1 &
fi
xvfb_pid=$!

i=0
while [ "$i" -lt 100 ]; do
    [ -S "/tmp/.X11-unix/X$display" ] && break
    if ! kill -0 "$xvfb_pid" 2>/dev/null; then
        cat "$cache/xvfb.log" >&2
        fail 'Xvfb exited before creating its display'
    fi
    sleep 0.1
    i=$((i + 1))
done
[ -S "/tmp/.X11-unix/X$display" ] || fail 'Xvfb did not create its display'

export DISPLAY=":$display"
log="$build_dir/render.log"
status=0
"$build_dir/bin/scancycle_viewer" \
    --frames 90 --capture "$build_dir/render.ppm" --capture-frame 60 \
    --compare-with "$repo_root/tests/data/scene_reference.ppm" --tolerance 6 \
    >"$log" 2>&1 || status=$?
cat "$log"
[ "$status" -eq 0 ] || exit "$status"

grep -Eq 'draws [1-9]' "$log" || fail 'the viewer reported no draw calls'
grep -Eq 'validation errors 0' "$log" || fail 'Vulkan validation reported errors'
grep -Eq 'compare : OK' "$log" || fail 'the golden-frame comparison failed'

printf 'lavapipe: CI-equivalent golden frame passed\n'
