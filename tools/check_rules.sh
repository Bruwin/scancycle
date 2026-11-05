#!/usr/bin/env bash
#
# The mechanical rules from AGENTS.md that a reviewer cannot be expected to catch
# by eye: no wall-clock or randomness reads in the simulation, and no include that
# reaches up the layer stack. CI runs this on every push and pull request, and
# AGENTS.md tells contributors to run it before every commit.
#
# It prints each hit as file:line and exits non-zero, so a violation fails the
# build without needing a parser or a build.

set -uo pipefail

status=0

report() {
    local name="$1"
    local hits="$2"
    if [ -n "$hits" ]; then
        echo "FAIL: $name"
        echo "$hits" | sed 's/^/    /'
        status=1
    else
        echo "ok:   $name"
    fi
}

# The PLC's own Time constructor is not a clock read: Value::time(...) and the
# Value time(...) declaration are excluded. Everything else that reaches for the
# clock or a random source is a failure.
clock='(^|[^_[:alnum:]])rand\(|random_device|std::chrono|(^|[^_[:alnum:]])time\('
clock="$clock|system_clock|steady_clock"

# Anything a layer may not include: a header from a layer above it.
above_core='#include "(platform|render|world|sim|plc|io|ui|lesson|assets|net|app)/'
above_l1='#include "(io|ui|lesson|assets|net|app)/'
above_l2='#include "(net|app)/'

report "determinism: no clock or randomness in sim, plc, world" \
    "$(grep -rnE "$clock" src/sim src/plc src/world \
        | grep -vE 'Value::time\(|Value time\(' || true)"

report "layering: core includes nothing above it" \
    "$(grep -rnE "$above_core" src/core || true)"

report "layering: platform, render, world, sim, plc include nothing above them" \
    "$(grep -rnE "$above_l1" src/platform src/render src/world src/sim src/plc || true)"

report "layering: io, ui, lesson, assets include no net or app header" \
    "$(grep -rnE "$above_l2" src/io src/ui src/lesson src/assets || true)"

exit "$status"
