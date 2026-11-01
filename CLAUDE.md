# Kladde

Regeln für alle eigenen Projekte: `../CLAUDE.md`. Dazu hier:

- Anforderungen: `docs/lastenheft.md` (Ids LF-xx, NF-xx in Commits und PRs nennen), Plan mit
  Häkchen: `docs/plan.md`. Beides aktuell halten.
- HealthLog ist PolyForm Noncommercial: lesen und verlinken ja, Code übernehmen nein. Die
  Pebble-App (coredevices/mobileapp) ist GPL-3.0 wie Kladde.
- Der Kern (`core/`) bleibt ohne Qt GUI, damit er im Android-Hintergrundjob und in Tests läuft.
- Echte Gesundheitsdaten (Exporte der Probe) nie committen; Fixtures nur synthetisch oder
  gekürzt und ohne Rückschluss auf Personen.
- `app/qml/Kante` nur über `tools/sync-kante.sh` ändern, nie von Hand.
- Temporäre Dateien und Build-Verzeichnisse unter `~/.cache/claude/kladde-*`.
