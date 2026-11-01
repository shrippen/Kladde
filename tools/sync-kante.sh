#!/usr/bin/env bash
# Copies Kante's QML module unchanged into the app and writes the resource file that
# compiles it into the binary, so `import Kante` works without an import path (as in Kaiwa).
#
#   tools/sync-kante.sh [path to the Kante checkout, default ../Kante]
#
# CI runs it against Kante's main and fails when app/qml/Kante differs.
set -euo pipefail
cd "$(dirname "$0")/.."
KANTE=${1:-../Kante}
[ -f "$KANTE/qml/Kante/qmldir" ] || { echo "no Kante checkout at $KANTE" >&2; exit 1; }
rm -rf app/qml/Kante
cp -r "$KANTE/qml/Kante" app/qml/Kante
{
  echo '<!DOCTYPE RCC><RCC version="1.0">'
  echo '<qresource prefix="/qt/qml">'
  (cd app/qml && find Kante -type f | LC_ALL=C sort | sed 's|.*|  <file>&</file>|')
  echo '</qresource>'
  echo '</RCC>'
} > app/qml/kante.qrc
echo "synced app/qml/Kante from $KANTE ($(git -C "$KANTE" describe --always 2>/dev/null || echo unknown))"
