#!/bin/sh
# Kladde with the Studio Weber demo data: no server, no keychain, own settings.
#   demo/start.sh [de|en]
# With SHOT_DIR and SHOT_PLAN (demo/tools/screenshots.py in shrippen.github.io) it takes the
# shots from demo/shots.json and quits. The demo build never ships (KLADDE_DEMO is off by
# default; the release build checks for it).
set -eu
cd "$(dirname "$0")/.."
build=${KLADDE_DEMO_BUILD:-build-demo}
cmake -S . -B "$build" -G Ninja -DKLADDE_DEMO=ON -DKLADDE_TESTS=OFF >/dev/null
cmake --build "$build" >/dev/null
export DEMO_LANG="${1:-${DEMO_LANG:-de}}"
if [ -n "${SHOT_PLAN:-}" ] && [ -z "${QT_QPA_PLATFORM:-}" ]; then
    export QT_QPA_PLATFORM=offscreen
fi
exec "$build/app/kladde"
