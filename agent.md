# Working rules for this repository

The rules for all shrippen repositories are in `RULES.md` (writing, code, commits, bug
fixes, repository, GUI). It is a synced copy from the rules repository; do not edit it
here. This file holds only what is particular to this repository. A Claude Code session on
the owner's machine has the rules through the global `CLAUDE.md` already and reads
`RULES.md` only for its GUI rule, before work on a user interface.

## Rules of this project

- Every visualization (chart, graph, heatmap) in a popup or page has axis labels or a legend; without them it is useless. Give it hover tooltips where possible. Tiles stay without axes and legends, so they read at a glance; their popup carries the details.

- Test integrations against a real instance where possible. HealthLog: the owner's instance `https://hl.arianw.de` with the `measurements:write` token in `token.txt` (repo root, ignored by Git; owner, 1.11.2026: free to test with it). Write only synthetic data dated far in the past (March 2001, ids starting `kladde-test-`), so it never shows in the owner's current charts; never print, log or commit the token. Without the token the test is skipped: in cloud environments and CI, tests against fakes are enough.

## Kladde – Projekt-Wissen

## Zweck
Bringt Gesundheitsdaten der Pebble in die eigene HealthLog-Instanz: auf Android aus
Health Connect, später auf Plasma Mobile direkt von der Uhr (libpebble3-Daemon über D-Bus).
Anforderungen: `docs/lastenheft.md` (Ids LF-xx, NF-xx in Commits und PRs nennen),
Plan mit Häkchen: `docs/plan.md`, Quellen: `docs/referenzen.md`. Aktuell halten.

## Schichten (nur zum direkten Nachbarn darunter)
```
app/          Kirigami-Oberfläche (QML mit Kante), Connection und Co. als QML-Objekte
  ↓                          ↓ (nur Android, JNI über app/syncbridge)
core/         HealthLogClient, Anmeldung, Abbildung, Batches (nur Qt Core + Network)
android/sync  Kotlin: Health Connect lesen, abbilden, Warteschlange, Upload (WorkManager)
```
- QML spricht nie HTTP; alles über Objekte aus `app/`, die `core/` nutzen.
- `core/` bleibt ohne Qt GUI, damit er in Tests und `kladde-push` läuft.
- Abbildung zweimal (Kotlin `Mapper.kt`, C++ `core/mapper.cpp`): beide prüfen gegen
  `tests/fixtures/pebble-day.expected.json`, eine Änderung immer an beiden.
- Der Pebble-Daemon (Plasma Mobile, später) liefert das Austauschformat (`core/hcjson.h`) an `core/`.

## Konventionen
- C++20, Qt 6. Vor jedem Commit: `cmake --build build && ctest --test-dir build`.
- HealthLog ist PolyForm Noncommercial: lesen und verlinken ja, Code übernehmen nein. Die
  Pebble-App (coredevices/mobileapp) ist GPL-3.0 wie Kladde.
- Echte Gesundheitsdaten (Exporte der Probe) nie committen; Fixtures nur synthetisch oder
  gekürzt und ohne Rückschluss auf Personen.
- `app/qml/Kante` nur über `tools/sync-kante.sh`, nie von Hand.
- Temporäre Dateien und Build-Verzeichnisse unter `~/.cache/claude/kladde-*`.

## Gelernte Fehler
- HealthLogs Batch-Endpunkt spricht HealthKit: Werte in Apples Einheit (`count/min`, `m`,
  Schlaf als Minuten mit `sleepStage` nach `HKCategoryValueSleepAnalysis`). Tagessummen
  (`stats:<id>:<Tag>`) überschreiben: immer den ganzen Tag schicken, nie ein Delta.
- Ein `measurements:write`-Token darf keine `source` nennen (422) und erreicht `/api/auth/me`
  nicht (403); die Zeitzone kommt dann vom Gerät.
- Gitea-Runner (act) löst `gradle/actions@v4` nicht auf („unsupported object type“); die CI
  läuft ohnehin auf GitHub.
- Idempotency-Key nie aus dem Body ableiten: HealthLog beantwortet einen bekannten Key 24 h aus dem
  Speicher, eine Tagessumme, die auf einen früheren Wert zurückfällt, käme nie an. Key je Batch neu,
  beim erneuten Senden derselbe.
- Nur 422 ist eine endgültige Ablehnung. Andere Status (404, 308, 400 …) halten die Warteschlange an,
  sonst leert eine falsche URL sie.
- Ein Datensatz über Mitternacht gehört zu dem Tag, zu dem der Mapper ihn zählt (Schritte: Starttag,
  Herzfrequenz: die Tage der Blöcke von Start bis Ende, ein Block gehört zum Tag seines Beginns), nicht zu dem Tag, für den er gelesen wurde.
- Die Warteschlange geht streng der Reihe nach: Tagessummen und Herzfrequenz-Blöcke überschreiben auf
  dem Server, ein neuerer Batch darf einen älteren im Backoff nicht überholen. Ein ersetzter Batch
  behält seinen Platz.
- Eine 200 ist nicht immer ganz erledigt: Einträge mit `status: failed` bleiben in der Warteschlange.

## Release

Release steps: `RELEASE.md`. Before a release, load the skill `release-changelog`. A release
is published, so the owner confirms it in words.

## Repository (this project)

- CI runs on the GitHub mirror (`.github/workflows/ci.yml`, since 1.11.2026) and reports back to Gitea as the commit status `github / test`. Gitea runs no Actions here (no `.gitea/workflows/`, Actions off in the repository settings).
- The merge waits for: CI is green on the PR's latest commit: every CI check that runs on each push (such as `github / test` from the GitHub mirror) shows success there. No status there yet means wait, not merge; if it never comes, check the mirror or ask the owner. PR-Agent's status does not count here. Without CI, the local checks count (here the build and `ctest`).
- Everything else: `RULES.md` (Repository) and the skill `pr-review`.
