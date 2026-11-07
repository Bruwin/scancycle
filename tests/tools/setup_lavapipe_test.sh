#!/usr/bin/env bash
# Regression coverage for tools/setup_lavapipe.sh that needs no apt, Vulkan,
# Xvfb or network. The stubs make the package-cache branches observable.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
setup_script="$repo_root/tools/setup_lavapipe.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

fail() {
    printf 'setup_lavapipe_test: %s\n' "$*" >&2
    exit 1
}

stubs="$tmp/bin"
cache="$tmp/cache"
apt_log="$tmp/apt.log"
dpkg_log="$tmp/dpkg.log"
mkdir -p "$stubs" "$cache/cache/archives"

export APT_LOG="$apt_log"
export DPKG_LOG="$dpkg_log"
export ARCHIVES="$cache/cache/archives"

cat >"$stubs/apt-get" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$APT_LOG"
case " $* " in
    *" install "*)
        : >"$ARCHIVES/base_1.0_amd64.deb"
        ;;
    *" download "*)
        for arg in "$@"; do
            case "$arg" in
                libvulkan1|xkb-data|x11-xkb-utils)
                    : >"$ARCHIVES/${arg}_1.0_amd64.deb"
                    ;;
            esac
        done
        ;;
esac
STUB

cat >"$stubs/dpkg-deb" <<'STUB'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$DPKG_LOG"
prefix="$3"
mkdir -p "$prefix/usr/include/vulkan" \
    "$prefix/usr/lib/x86_64-linux-gnu" \
    "$prefix/usr/share/vulkan/icd.d" \
    "$prefix/usr/share/vulkan/explicit_layer.d" \
    "$prefix/usr/bin"
: >"$prefix/usr/include/vulkan/vulkan.h"
: >"$prefix/usr/lib/x86_64-linux-gnu/libvulkan.so"
: >"$prefix/usr/share/vulkan/icd.d/lvp_icd.json"
: >"$prefix/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json"
printf '#!/bin/sh\n' >"$prefix/usr/bin/Xvfb"
printf '#!/bin/sh\n' >"$prefix/usr/bin/xkbcomp"
chmod +x "$prefix/usr/bin/Xvfb" "$prefix/usr/bin/xkbcomp"
STUB

for command in cmake unshare; do
    printf '#!/usr/bin/env bash\nexit 0\n' >"$stubs/$command"
done
chmod +x "$stubs/apt-get" "$stubs/dpkg-deb" "$stubs/cmake" "$stubs/unshare"

# A PATH that can resolve the commands the script needs before its preflight,
# but deliberately cannot find apt-get.
minimal="$tmp/minimal"
mkdir -p "$minimal"
ln -s "$(command -v dirname)" "$minimal/dirname"
ln -s "$(command -v getconf)" "$minimal/getconf"

# Missing apt-get must fail before creating the cache or downloading anything.
set +e
PATH="$minimal" SC_LAVAPIPE_CACHE="$tmp/no-apt-cache" \
    SC_LAVAPIPE_BUILD="$tmp/no-apt-build" \
    /bin/bash "$setup_script" --setup-only >"$tmp/no-apt.log" 2>&1
status=$?
set -e
[ "$status" -ne 0 ] || fail 'missing apt-get was accepted'
grep -q 'apt-get is required' "$tmp/no-apt.log" || fail 'missing apt-get was not reported'
[ ! -e "$tmp/no-apt-cache" ] || fail 'preflight created the cache'

# A missing CMake must also fail before touching the package cache.
set +e
PATH="$stubs:$PATH" CMAKE="$tmp/not-a-cmake" \
    SC_LAVAPIPE_CACHE="$tmp/no-cmake-cache" \
    SC_LAVAPIPE_BUILD="$tmp/no-cmake-build" \
    bash "$setup_script" --setup-only >"$tmp/no-cmake.log" 2>&1
status=$?
set -e
[ "$status" -ne 0 ] || fail 'missing CMake was accepted'
grep -q 'CMake not found' "$tmp/no-cmake.log" || fail 'missing CMake was not reported'
[ ! -e "$tmp/no-cmake-cache" ] || fail 'CMake preflight created the cache'

run_setup() {
    PATH="$stubs:$PATH" \
        CMAKE="$stubs/cmake" \
        SC_LAVAPIPE_CACHE="$cache" \
        SC_LAVAPIPE_BUILD="$tmp/build" \
        bash "$setup_script" "$@"
}

if ! run_setup --setup-only >"$tmp/first.log" 2>&1; then
    cat "$tmp/first.log" >&2
    exit 1
fi
grep -q 'lavapipe: setup complete' "$tmp/first.log" || fail 'setup did not complete'
grep -q 'noble main universe' "$cache/etc/sources.list" || fail 'sources.list was not written'
grep -qE ' download (libvulkan1|xkb-data|x11-xkb-utils)' "$apt_log" \
    || fail 'missing runtime packages were not downloaded explicitly'
[ "$(wc -l <"$dpkg_log")" -eq 4 ] || fail 'the base and runtime packages were not extracted'
[ -f "$cache/root/usr/include/vulkan/vulkan.h" ] || fail 'Vulkan header was not staged'
[ -x "$cache/root/usr/bin/xkbcomp" ] || fail 'xkbcomp was not staged'

# A second run must reuse the cached runtime packages instead of downloading.
: >"$apt_log"
run_setup --setup-only >"$tmp/second.log" 2>&1
if grep -q ' download ' "$apt_log"; then
    fail 'cached runtime packages were downloaded again'
fi

printf 'lavapipe setup test: ok\n'
