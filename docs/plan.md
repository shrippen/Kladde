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
- [x] Upload mit `kladde-push` nach hl.arianw.de mit synthetischen Daten vom März 2001:
      10 eingefügt, beim zweiten Lauf 0 neu (5 Duplikate, 5 überschrieben). Zeitzone: das
      Mess-Token erreicht `/api/auth/me` nicht, `kladde-push` nimmt die des Rechners.
- [ ] Der erste echte Upload.
- [ ] Echten Export (gekürzt, anonymisiert) als zweite Fixture.
- [x] Entscheidung Hintergrund-Sync: Kotlin-Modul `android/sync` (lesen, abbilden, Warteschlange,
      Upload per WorkManager). Den Qt-Kern ohne Oberfläche im Worker zu laden, braucht eine
      eigene QCoreApplication neben der der App und ist ohne Gerät nicht prüfbar. Kotlin und
      C++ bilden gleich ab: beide prüfen gegen `tests/fixtures/pebble-day.expected.json`.
- [ ] Issue bei MBombeck/HealthLog entwerfen (Text vorher freigeben): Quelle `HEALTH_CONNECT`
      für native Clients über den Batch-Endpunkt.

## Phase 1: Android (Muss und Soll, LF-01 … LF-33)

Aufbau nach der iOS-App ([ios-aufbau.md](ios-aufbau.md)).

- [x] `android/sync`: Health Connect mit Changes-Token, ganze Tage für Summen und Blöcke,
      Warteschlange in SQLite (Cursor in derselben Transaktion), Upload mit Backoff, 422 je
      Eintrag, Token im Android Keystore, WorkManager, Statusdaten (LF-10 … LF-24).
- [ ] Qt für Android bauen (Vorlage Plasmai), JNI zu `KladdeSync`.
- [x] Einrichtung: Server prüfen, Anmeldung mit Passwort und zweitem Faktor; Refresh-Token im
      Schlüsselbund (QtKeychain), Erneuerung einzeln und mit Rotation; nach der Anmeldung legt
      die App ihr eigenes Mess-Token für den Hintergrund an.
- [ ] Web-Login mit PKCE (`healthlog://login-callback`), Health-Connect-Rechte im Ablauf.
- [x] Start (Kacheln mit letzten 7 Tagen), Erfassen (Gewicht, Blutdruck mit Puls, Puls,
      Körperfett, Stimmung; Bereiche geprüft), Verlauf (Woche bis Jahr, Achse, Kennzahlen),
      Mehr (Sync-Status, Konto). Deutsch und Englisch.
- [x] Demo-Modus mit Studio-Weber-Gesundheitsdaten (`demo/start.sh`, `demo/shots.json`), ein
      Test hält die Demo aus normalen Builds.
- [ ] Android-Schlüsselspeicher für das Refresh-Token (JNI zu `Credentials`).

## Phase 2: Anzeige und Plasma Mobile (LF-30 … LF-33)

Hypnogramm der letzten Nacht (neuer Baustein in Kante; die Phasen liegen schon in der
Demo-Welt). Linux-Paket für aarch64.

## Phase 3: Pebble direkt an Plasma Mobile (eigenes Repo)

Daemon aus libpebble3 (Vorlage Rockpool, Lizenz klären) für Plasma Mobile mit BlueZ und
D-Bus; danach in Kladde eine Datenquelle, die den Daemon liest (LF-16).

## Veröffentlichung

Arbeits-Repo auf Gitea, öffentlicher Spiegel github.com/shrippen/Kladde. CI nur auf GitHub,
Status `github / test` zurück nach Gitea (`agent.md`). Release-APK später nur über GitHub
Actions. Lizenz GPL-3.0-or-later (wie die Pebble-App).
