#!/bin/sh
# Regenerates the committed SPIR-V blobs. The build never needs a shader
# compiler: it reads these files at run time, so a checkout without the Vulkan
# SDK still runs.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$ROOT/src/render/vulkan/shaders"
OUT="$SRC/compiled"
GLSLANG=${GLSLANG:-glslang}
mkdir -p "$OUT"
for shader in chunk.vert chunk.frag cull.comp; do
    "$GLSLANG" -V --target-env vulkan1.3 -Os -o "$OUT/$shader.spv" "$SRC/$shader"
done
echo "shaders rebuilt into $OUT"
