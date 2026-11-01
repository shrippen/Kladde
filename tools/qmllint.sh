#!/usr/bin/env bash
# qmllint over Kladde's own QML (Kante has its own run), as the CI runs it. Exit code 1 on any
# finding. JSON with every finding: tools/qmllint.sh --json -
#
# Off, on purpose (as in Kante/tools/qmllint.sh):
#   unqualified, missing-property   the app's objects come in as context properties
#                                   (account, dashboard, history, entry, sync).
#   block-scope-var-declaration     Qt 6.12 reports every `var` in a block; switch file by file.
# Files importing Kladde.Core run without the import check: the app registers that module
# from C++ at run time, qmllint cannot see it.
# Note: the plain text output of Qt 6.12's qmllint leaves out findings the exit code counts.
set -uo pipefail
cd "$(dirname "$0")/.."
QMLLINT=/usr/lib/qt6/bin/qmllint
[ -x "$QMLLINT" ] || QMLLINT=$(command -v qmllint6 || command -v qmllint || echo /usr/lib64/qt6/bin/qmllint)
Q=("$QMLLINT" --unqualified disable --missing-property disable --block-scope-var-declaration disable
   --max-warnings 0 -I app/qml "$@")
mapfile -t core < <(grep -l '^import Kladde.Core' app/qml/*.qml)
mapfile -t others < <(grep -L '^import Kladde.Core' app/qml/*.qml)
rc=0
"${Q[@]}" "${others[@]}" || rc=1
"${Q[@]}" --import disable "${core[@]}" || rc=1
[ $rc = 0 ] && echo "qmllint: no findings"
exit $rc
