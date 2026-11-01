# Review guide

Every PR is reviewed before it is merged (rule in `agent.md`). PR-Agent is
not set up here, so a new Claude agent reviews every round. The reviewer
knows nothing of the session that wrote the change: it gets the PR's
intent, its diff and the repository, and this file. The owner does not
read code; this review is the second reader.

## What failure looks like here

Kladde moves one person's health data from the phone (or the watch) into
their own HealthLog. A bug that matters is one that costs data or trust:

- **Wrong data:** a value in the wrong unit, on the wrong day (time zone,
  clock change), a sleep stage mapped wrong, a day total built from a
  partial day (it overwrites the full one), a heart-rate bucket on the
  wrong boundary.
- **Duplicates or loss:** an `externalId` that changes between syncs, an
  `Idempotency-Key` that differs for the same batch, a sync cursor or
  changes token that moves before the server confirmed, a 422 that drops
  more entries than the ones it named.
- **Leaks:** health data or a token in a log, a commit, a fixture, a
  crash report or a request to anyone but the user's server; a token in a
  settings file instead of the keychain; demo data in a release build.
- **Writes where it must not:** a test that writes to the user's own
  HealthLog (`hl.arianw.de`) instead of a throwaway container.
- **Drains the phone:** polling, full re-reads instead of the changes
  API, work on the UI thread, a background job that never finishes.
- **Lies:** docs, `docs/lastenheft.md`, `docs/plan.md` or comments the
  code contradicts.

## What to check in particular

- Layers (`agent.md`): QML never talks HTTP; sources hand over only the
  exchange format (`core/hcjson.h`); `core/` links no Qt GUI module.
- The mapping against HealthLog's contract (`docs/referenzen.md`,
  `docs/api/openapi.yaml` in MBombeck/HealthLog): identifiers, units,
  `externalId` forms, limits (500 entries, 120 characters).
- GUI rule: Kante as it is, no own colours, fonts or sizes, no edit in
  `app/qml/Kante`. Charts have axis labels or a legend.
- Bug fixes: a test that failed before the fix, and the cause removed.

## Skip

- `app/qml/Kante/` and `app/qml/kante.qrc` (`tools/sync-kante.sh`).
- `spikes/`: throwaway code for phase 0; only check that it reads and
  never writes health data, and leaks nothing.
- Images.

## How to report

- Every finding names file and line and the concrete input or state that
  goes wrong. No guess without evidence; if unsure, say so.
- Each finding gets one label: **Wichtig** (a bug from the list above,
  must be fixed or answered before the merge), **Kleinigkeit** (optional,
  at most five) or **Bestand** (the bug was there before this PR; not
  blocking).
- The report starts with the count, e.g. "2 Wichtig, 1 Kleinigkeit" (the
  session puts the heading above it). The count of **Wichtig** holds every
  one still open, also from an earlier round. Only when none is open it
  says plainly: "Nichts zu beheben."
- In a later round: say for each open **Wichtig** of the last round whether
  it is fixed, or whether its answer is accepted, with the reason.
- Findings in German, code names as they are.
