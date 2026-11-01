# Plan

Stand: 1. November 2026. Anforderungen: [lastenheft.md](lastenheft.md), Quellen:
[referenzen.md](referenzen.md).

## Aufbau

```
core/      C++ (nur Qt Core + Network): Austauschformat, Abbildung auf HealthLog, Batches, Client
tools/     kladde-push (Export -> HealthLog, ohne App), sync-kante.sh
app/       Kirigami-App mit Kante (Android und Plasma Mobile)
android/   (Phase 1) Kotlin: Health-Connect-Leser, WorkManager, Berechtigungen
spikes/    Wegwerf-Code für Phase 0 (hc-probe)
```

Jede Datenquelle liefert JSON im Austauschformat (`core/hcjson.h`): der Kotlin-Leser auf
Android, später der Adapter für den Pebble-Daemon auf Plasma Mobile. Ab da ist alles gleich.

## Phase 0: Machbarkeit

- [x] HealthLog-API gelesen: `POST /api/measurements/batch` mit HealthKit-Bezeichnern,
      `measurements:write`-Token als Brücke (Quelle `EXTERNAL`).
- [x] Offizielle Pebble-App gelesen (GPL-3.0): schreibt Steps, HeartRate, Sleep, Exercise.
- [x] Kern: Abbildung (Tagessummen, 10-Minuten-Herzfrequenz, Schlafphasen), Batches mit
      Idempotency-Key, Client, Tests.
- [x] `kladde-push`: Export nach HealthLog vom Desktop.
- [x] App-Gerüst mit Kante, Screenshot ohne Display für die CI.
- [x] `spikes/hc-probe`: zählt je Typ und App, exportiert JSON.
- [ ] Probe auf dem Telefon: Was schreibt die Pebble wirklich (coredevices/mobileapp#469)?
- [ ] Erster echter Upload mit `kladde-push` nach hl.arianw.de, Ergebnis in HealthLog prüfen
      (Tagessummen, Herzfrequenz-Blöcke, Schlafphasen, Zeitzone).
- [ ] Echten Export (gekürzt, anonymisiert) als zweite Fixture.
- [ ] Spike: Kann ein WorkManager-Job den C++-Kern ohne Oberfläche laden? Sonst schreibt Kotlin
      in eine SQLite-Warteschlange und der Kern lädt hoch.
- [ ] Issue bei MBombeck/HealthLog entwerfen (Text vorher freigeben): Quelle `HEALTH_CONNECT`
      für native Clients über den Batch-Endpunkt.

## Phase 1: Android-MVP (LF-01 … LF-24)

Qt für Android (Vorlage: Plasmai, `scripts/build-android.sh`), Kotlin-Leser mit Changes-API,
WorkManager, Token im Keystore (QtKeychain), Warteschlange, Statusseite.

## Phase 2: Anzeige und Plasma Mobile (LF-30 … LF-33)

Heute, Verläufe, Hypnogramm (neuer Baustein in Kante), manuelle Einträge. Linux-Paket für
aarch64. Demo-Modus mit Studio-Weber-Gesundheitsdaten, `demo/shots.json`.

## Phase 3: Pebble direkt an Plasma Mobile (eigenes Repo)

Daemon aus libpebble3 (Vorlage Rockpool, Lizenz klären) für Plasma Mobile mit BlueZ und
D-Bus; danach in Kladde eine Datenquelle, die den Daemon liest (LF-16).

## Veröffentlichung

Erst privat auf Gitea, CI dort. Beim Schritt nach öffentlich: CI nach `.github/workflows`,
Release-APK über GitHub Actions, Lizenz GPL-3.0-or-later (wie die Pebble-App).
