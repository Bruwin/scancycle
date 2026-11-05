#!/usr/bin/env bash
# Rebuilds the distributable artifacts and proves they work.
#
# Both were hand-made snapshots of whatever working tree happened to be on disk,
# so a licence change left them carrying the old terms and nothing noticed. They
# are now built from a clone of a committed ref - what ships is what is in git -
# and verified by cloning and extracting the result.
#
# Usage: tools/build_artifacts.sh [ref]      ref defaults to main.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ref="${1:-main}"
bundle="$repo_root/artifacts/scancycle.bundle"
tarball="$repo_root/artifacts/scancycle-full-repo.tar.gz"

fail() {
    printf 'build_artifacts: %s\n' "$*" >&2
    exit 1
}

cd "$repo_root"

if [ -n "$(git status --porcelain)" ]; then
    printf 'build_artifacts: warning: uncommitted changes are not archived;\n' >&2
    printf '                only committed content in %s is in the artifacts.\n' "$ref" >&2
fi

target="$(git rev-parse --verify --quiet "${ref}^{commit}")" || fail "no such ref: $ref"
short="$(git rev-parse --short "$target")"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

# A linked worktree has a .git file rather than a directory and git can refuse to
# clone one, so fall back to the shared git directory.
if ! git clone -q --no-hardlinks --single-branch --branch "$ref" "$repo_root" \
        "$scratch/scancycle" 2>/dev/null; then
    rm -rf "$scratch/scancycle"
    git clone -q --no-hardlinks --single-branch --branch "$ref" \
        "$(git rev-parse --git-common-dir)" "$scratch/scancycle" \
        || fail "cannot clone $ref into a scratch directory"
fi

snapshot="$scratch/scancycle"
[ "$(git -C "$snapshot" rev-parse HEAD)" = "$target" ] || fail "the clone is not at $short"

# The clone's remote is a path on this machine, which means nothing to whoever
# receives the artifact.
git -C "$snapshot" remote remove origin

mkdir -p "$(dirname "$bundle")"

printf 'building artifacts from %s (%s)\n' "$ref" "$short"

# HEAD has to be in the bundle alongside the branch: a bundle of a bare branch
# name clones to an empty repository on an unborn branch, which is how this file
# first managed to produce an artifact that looked fine and was useless.
git -C "$snapshot" bundle create "$bundle" HEAD "$ref" >/dev/null
tar czf "$tarball" -C "$scratch" scancycle

printf '\nverifying\n'

clone="$scratch/clone"
git clone -q "$bundle" "$clone"
[ "$(git -C "$clone" rev-parse HEAD)" = "$target" ] || fail "the bundle does not clone to $short"
[ -f "$clone/CMakeLists.txt" ] || fail "the bundle has no sources"
grep -q 'LESSER GENERAL PUBLIC LICENSE' "$clone/LICENSE" || fail "the bundle's LICENSE is not the LGPL"
grep -q 'GNU GENERAL PUBLIC LICENSE' "$clone/COPYING" || fail "the bundle has no GPL COPYING"

mkdir -p "$scratch/extract"
tar xzf "$tarball" -C "$scratch/extract"
packed="$scratch/extract/scancycle"
[ "$(git -C "$packed" rev-parse HEAD 2>/dev/null)" = "$target" ] \
    || fail "the tarball does not contain a repository at $short"
[ -z "$(git -C "$packed" status --porcelain)" ] || fail "the tarball's working tree is dirty"
grep -q 'LESSER GENERAL PUBLIC LICENSE' "$packed/LICENSE" || fail "the tarball's LICENSE is not the LGPL"
grep -q 'GNU GENERAL PUBLIC LICENSE' "$packed/COPYING" || fail "the tarball has no GPL COPYING"

# Read the listing once: piping tar into grep -q under pipefail fails the
# pipeline when grep exits early, which would hide the very match being tested.
listing="$(tar tzf "$tarball")"
if printf '%s\n' "$listing" | grep -qE '/build/|/artifacts/'; then
    fail "the tarball contains build output or previous artifacts"
fi

printf '  %-10s %8s bytes, %s refs, clones to %s\n' bundle "$(wc -c <"$bundle")" \
    "$(git bundle list-heads "$bundle" | wc -l)" "$(git -C "$clone" rev-parse --short HEAD)"
printf '  %-10s %8s bytes, %s entries, extracts to %s\n' tarball "$(wc -c <"$tarball")" \
    "$(printf '%s\n' "$listing" | wc -l)" "$(git -C "$packed" rev-parse --short HEAD)"
printf '  %-10s %s\n' licence "$(grep -m1 'Licence:' "$packed/README.md" | sed 's/^[* ]*//')"
printf '\nboth artifacts carry %s and are in %s\n' "$short" "$(dirname "$bundle")"
