<p align="center"><img src="icon.svg" alt="Kladde" width="96" height="96"></p>

# Kladde

_Deine Gesundheitsdaten in deinem eigenen HealthLog_

Kladde bringt deine Gesundheitsdaten im Hintergrund in deine eigene [HealthLog](https://healthlog.dev)-Instanz und
zeigt deine Werte auf dem Telefon. Auf **Android** kommen die Daten aus **Health Connect**, auf **Plasma Mobile**
(später) direkt von der **Pebble**. Eine Qt-/Kirigami-App im Design [Kante](https://github.com/shrippen/Kante)
für beide. [English](../README.md) · [Landing Page](https://shrippen.github.io/Kladde/)

## Was abgeglichen wird

Auf Android liest Kladde Health Connect von jeder App. Tagessummen und Puls-Blöcke rechnet Health Connect selbst, nach
der in Health Connect eingestellten App-Reihenfolge, damit Telefon und Uhr nie doppelt zählen. Schlaf kommt von jeder
App; sahen zwei dieselbe Nacht, gilt die erste in Kladdes Reihenfolge (Pebble-App, Sleep as Android, Google Fit). HealthLogs Batch-Endpunkt spricht HealthKit, darum kommen die Werte in Apples Einheiten und mit der Quelle
„Apple Health“ an.

| Health Connect | In HealthLog | Wie |
|---|---|---|
| Schritte | Schritte | eine Summe je Tag (Ortszeit), bei jedem Lauf der ganze Tag |
| Strecke | Geh- und Laufstrecke | eine Summe je Tag, in Metern |
| Aktive Kalorien, Stockwerke | Aktive Energie, Stockwerke | eine Summe je Tag (wenn erlaubt) |
| Herzfrequenz | Puls | 10-Minuten-Blöcke mit Mittel, Minimum und Maximum |
| Ruhepuls, Gewicht, Körperfett, Magermasse, Blutdruck, Blutzucker, Sauerstoffsättigung, HRV (RMSSD), Atemfrequenz, VO₂max, Körpertemperatur | dasselbe | ein Eintrag je Messung, von jeder App |
| Schlaf mit Phasen | Schlaf | Minuten je Phase (wach, Kern, tief, REM); gesendet vier Stunden nach dem Ende der Nacht, damit die der Uhr da ist |
| Training (Freeletics, Pebble-App, Google Fit …) | Workout | Sportart, Zeit, Pause, Puls, Schritte; Abschnitte eines Trainings (Freeletics) als eines; bei Überschneidung gilt die App, die vorne steht (Freeletics, Pebble-App, Google Fit); gesendet zwei Stunden nach dem Ende |

## Die App

- **Anmeldung** wie mit der iOS-App: erst wird der Server geprüft, dann Passwort mit zweitem Faktor (TOTP oder
  Wiederherstellungscode) oder Single Sign-on im Browser. Kladde behält nur eine erneuerbare Anmeldung, verschlüsselt
  mit einem Schlüssel im Android Keystore.
- **Abgleich im Hintergrund** alle 60 Minuten (ab 15), beim ersten Lauf 30 Tage zurück, danach nur, was sich geändert
  hat; „Jetzt abgleichen“ und „Alles neu lesen“ von Hand.
- **Warteschlange:** was nicht gesendet werden konnte, wartet und geht später raus, streng der Reihe nach und ohne
  Duplikate.
- **Status:** letzter Lauf, wartende Einträge, Fehler, eine Warnung, wenn eine Art Daten seit zwei Tagen fehlt.
- **Start** (neuester Wert und letzte sieben Tage je Messgröße), **Verlauf** (eine Woche bis ein Jahr, mit Skala,
  Legende und Kennzahlen), **Erfassen** (Gewicht, Blutdruck, Puls, Körperfett, Stimmung). Deutsch und Englisch.

## So funktioniert es (kurz)

```
app/          Kirigami-Oberfläche (QML mit Kante)
  ↓
core/         HealthLog-Client (Anmeldung, Werte, Erfassen), Abbildung, Batches
              (nur Qt Core + Network; auch kladde-push, später der Pebble-Daemon)

android/sync  Kotlin, nur Android: der Abgleich aus Health Connect im Hintergrund
```

Auf Android liest das Kotlin-Modul `android/sync` Health Connect in einem WorkManager-Job, bildet die Datensätze ab wie
`core/mapper.cpp` (gemeinsame Fixtures halten beide gleich), reiht sie ein und schickt sie an `/api/measurements/batch`, Training an `/api/workouts/batch`.
`core/` bildet ab und schickt für `kladde-push` (ohne Warteschlange: es hört beim ersten Fehler auf) und später den Pebble-Daemon.
Anforderungen: [lastenheft.md](lastenheft.md), Plan: [plan.md](plan.md).

## Installieren

Die APK aus dem [neuesten Release](https://github.com/shrippen/Kladde/releases/latest). Braucht Android 14 oder neuer
auf einem Telefon mit 64-Bit-ARM, die Pebble-App, die nach Health Connect schreibt, und HealthLog 1.46 oder neuer. In
der App: Server eingeben, anmelden, Health Connect erlauben.

## Entwicklung

```sh
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build   # Desktop, Tests
scripts/build-android.sh debug     # „Kladde Dev“ (de.arianw.kladde.dev), neben einem Release installierbar
adb install -r dist/android/kladde-debug.apk
```

- Desktop: Qt 6 (Core, Network, Quick, QuickControls2, Test, LinguistTools), QtKeychain, zur Laufzeit Kirigami.
  `-DKLADDE_APP=OFF` baut nur Kern, Tests und `kladde-push`.
- Android: `scripts/build-android.sh` installiert, was fehlt (NDK, Qt per aqtinstall, KF6, QtKeychain, OpenSSL).
- Demo-Modus für Screenshots (kein Server, Demodaten „Studio Weber“): `demo/start.sh [de|en]`.
- Releases baut, signiert und hängt nur GitHub Actions an (`.github/workflows/release.yml`); Ablauf in
  [agent.md](../agent.md). Änderungen: [CHANGELOG.md](../CHANGELOG.md).
- Werkzeuge aus Phase 0: `spikes/hc-probe` zeigt und exportiert, was in Health Connect steht, `kladde-push` lädt so
  einen Export hoch (`--dry-run` zeigt die Batches). Exporte sind echte Gesundheitsdaten: nie ins Repo.

## Lizenz

GPL-3.0-or-later, siehe [LICENSE](../LICENSE), wie die Pebble-App. HealthLog steht unter PolyForm Noncommercial:
Kladde spricht nur mit seiner API und enthält keinen Code daraus.

## Dank

[HealthLog](https://healthlog.dev), die [Pebble-App](https://github.com/coredevices/mobileapp) von Core Devices,
[Qt](https://www.qt.io/), [Kirigami](https://invent.kde.org/frameworks/kirigami),
[QtKeychain](https://github.com/frankosterfeld/qtkeychain), [Health Connect](https://developer.android.com/health-and-fitness/guides/health-connect).
