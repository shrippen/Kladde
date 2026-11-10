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

Das Austauschformat (`core/hcjson.h`) nutzen `kladde-push` und später der Adapter für den
Pebble-Daemon. Auf Android bildet `android/sync` selbst ab (Entscheidung unten); gleich
bleibt die Abbildung über die gemeinsame Fixture.

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
- [ ] **Dringend (Owner, 1.11.2026): Upstream-PR bei MBombeck/HealthLog**, damit native Clients
      und Mess-Tokens mehr Quellen als `APPLE_HEALTH`/`MANUAL`/`EXTERNAL` nennen können (z. B.
      `HEALTH_CONNECT`). Heute landet die Uhr nur über ein Mess-Token richtig (`EXTERNAL`); mit
      der Sitzung der App stünde sie als `APPLE_HEALTH` da. Text und Einreichung vorher freigeben.
- [ ] Issue bei MBombeck/HealthLog (v1.46.2): `readActivityDayKeys` (`summary-reads.ts`) fragt je
      Tag `user_id` + `measured_at` ab; Postgres wählte `measurements_live_covering_idx`
      (`user_id, type, measured_at`) und las für jeden der 365 Tage den ganzen Index des Nutzers.
      Mit aufgeblähter Tabelle (85 000 Zeilen, 12 GB) lief `/api/dashboard/summary` in den
      60-s-Timeout (HTTP 500), ausgelöst nach dem ersten Upload von Kladde (1.11.2026).
      `VACUUM FULL measurements` behob es (82 MB, 2,4 ms). Text vorher freigeben.

## Phase 1: Android (Muss und Soll, LF-01 … LF-33)

Aufbau nach der iOS-App ([ios-aufbau.md](ios-aufbau.md)).

- [x] `android/sync`: Health Connect mit Changes-Token, ganze Tage für Summen und Blöcke,
      Warteschlange in SQLite (Cursor in derselben Transaktion), Upload mit Backoff, 422 je
      Eintrag, Token im Android Keystore, WorkManager, Statusdaten (LF-10 … LF-24).
- [x] Qt für Android bauen (Vorlage Plasmai, `scripts/build-android.sh`), JNI zu `KladdeSync`
      (`app/syncbridge_android.cpp`); Debug-APK `de.arianw.kladde.dev` aus der CI.
- [ ] APK auf dem Telefon: Anmeldung (SSO geht), Health-Connect-Rechte, Sync.
- [x] SSO-Anmeldung (OIDC) wie die iOS-App: Browser mit PKCE, Rücksprung über
      `healthlog://oidc-callback`, Code gegen Tokens (Android).
- [x] Der Sync lädt mit der Sitzung der App hoch, ohne eigenes Mess-Token (Owner, 1.11.2026;
      HealthLog legt Mess-Tokens nur im Browser an). Das Token-Paar hält `TokenVault` in
      `android/sync` (Keystore, eine Sperre: App und Job verbrauchen das Refresh-Token nie
      doppelt). Folge bis zum Upstream-PR: die Werte der Uhr stehen als `APPLE_HEALTH` da.
- [x] Einrichtung: Server prüfen, Anmeldung mit Passwort und zweitem Faktor; Refresh-Token im
      Schlüsselbund (QtKeychain), Erneuerung einzeln und mit Rotation.
- [x] Vault (Review #6): Aufrufe der App in einer eigenen Warteschlange abseits des UI-Threads,
      ein verspätet zurückgegebenes Refresh-Token ersetzt nie das lebende, Widerruf beim Server,
      zu dem das Token gehört.
- [ ] Web-Login mit PKCE für Passwort und Passkey (`healthlog://login-callback`), SSO unter
      Linux, Health-Connect-Rechte im Ablauf.
- [x] Start (Kacheln mit letzten 7 Tagen), Erfassen (Gewicht, Blutdruck mit Puls, Puls,
      Körperfett, Stimmung; Bereiche geprüft), Verlauf (Woche bis Jahr, Achse, Kennzahlen),
      Mehr (Sync-Status, Konto). Deutsch und Englisch.
- [x] Demo-Modus mit Studio-Weber-Gesundheitsdaten (`demo/start.sh`, `demo/shots.json`), ein
      Test hält die Demo aus normalen Builds.
- [ ] Android-Schlüsselspeicher für das Refresh-Token: QtKeychain (Android Keystore) ist im
      APK, auf dem Telefon ungeprüft.

## 0.2: Alles aus Health Connect (Owner, 1.11.2026)

Kladde liest jede Art, die HealthLog speichern kann, von jeder App (z. B. Training aus
Freeletics), nicht nur von der Pebble-App. Entscheidungen des Owners:

- Doppelte über Health Connects App-Reihenfolge: Tagessummen (Schritte, Strecke, aktive
  Kalorien, Stockwerke) und die 10-Minuten-Blöcke der Herzfrequenz rechnet Health Connect
  selbst (`aggregateGroupByPeriod` / `…ByDuration`, beachtet die Reihenfolge in den
  Health-Connect-Einstellungen). Schlaf und Training: bei Überschneidung gewinnt die App, die
  in Kladde je Art vorne steht (Training: Freeletics, Pebble-App, Google Fit; Schlaf:
  Pebble-App, Sleep as Android, Google Fit). Einzelwerte (Gewicht, Blutdruck …) von
  jeder App.
- Rücklesen wie eingestellt (30 Tage); davor deckt der Import des Owners ab.

```
Changes-API (alle Apps) ─▶ geänderte Tage ─▶ aggregate je Tag/10 min ─▶ Tagessummen, Puls
                        └▶ Sitzungen (Schlaf, Training) ─▶ Überschneidung nach App-Wahl
                        └▶ Einzelwerte ─────────────────────────────────────────────────▶ Mapper
Mapper ─▶ /api/measurements/batch   |   Training ─▶ /api/workouts/batch (dieselbe Warteschlange)
```

- [x] Probe: welche App schreibt was (alle Typen, 30 Tage), vor allem Freeletics.
- [x] Tagessummen (Schritte, Strecke, aktive Kalorien, Stockwerke) und Puls über `aggregate` /
      `aggregateGroupByDuration` von allen Apps statt Summe der Pebble-Datensätze.
- [x] Einzelwerte: Gewicht, Körperfett, Magermasse, Blutdruck, Blutzucker, SpO₂, HRV,
      Atemfrequenz, VO₂max, Körpertemperatur, Ruhepuls von jeder App (`ReadingSource`, eigener
      Cursor, nur erlaubte Arten; Ids und Einheiten wie HealthLogs `apple-health-mapping.ts`).
- [x] Schlaf aller Apps, Überschneidung nach App-Reihenfolge (`SleepSource`, eigener Cursor,
      gesendet 4 h nach dem Ende; Auswahl mit Training gemeinsam in `Sessions.select`).
- [x] Training → `/api/workouts/batch` (Sportart, Pause, Puls, Schritte, Strecke), eigener
      Changes-Token, Batches mit Ziel in derselben Warteschlange (`WorkoutSource`, `WorkoutMapper`).
- [ ] Training: Energie (braucht `READ_*_CALORIES_BURNED`), gelöschte Sitzungen.
- [x] Berechtigungen je Art (Pflicht und optional, „Weitere Arten erlauben“).
- [x] App-Reihenfolge für Schlaf und Training unter „Mehr“ änderbar (`AppOrder`; gezeigt nur
      Apps, die die Art im Rücklesefenster geschrieben haben; gilt für Sitzungen, die danach
      beginnen, frühere behalten ihren Gewinner: `Ranking`).
- [ ] Status je Art.
- [ ] C++-Abbildung (`core/mapper.cpp`) und Fixture nachziehen; Lastenheft LF-14/LF-15.

## Phase 2: Anzeige und Plasma Mobile (LF-30 … LF-33)

Hypnogramm der letzten Nacht (neuer Baustein in Kante; die Phasen liegen schon in der
Demo-Welt). Linux-Paket für aarch64.

## Phase 3: Pebble direkt an Plasma Mobile (eigenes Repo)

Daemon aus libpebble3 (Vorlage Rockpool, Lizenz klären) für Plasma Mobile mit BlueZ und
D-Bus; danach in Kladde eine Datenquelle, die den Daemon liest (LF-16).

## Veröffentlichung

Arbeits-Repo auf Gitea, öffentlicher Spiegel github.com/shrippen/Kladde. CI nur auf GitHub,
Status `github / test` zurück nach Gitea (`agent.md`). Release-APK nur über GitHub Actions
(`release.yml`, Ablauf in `RELEASE.md`). Landing Page https://shrippen.github.io/Kladde/ (GitHub
Pages, `main`, `/docs`). Lizenz GPL-3.0-or-later (wie die Pebble-App).

- [x] 0.1.0: CHANGELOG, `release.yml`, Landing Page, README
