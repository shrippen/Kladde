# Changelog

The release notes of Kladde, newest first. Each section is the text of the release on Gitea and
GitHub and of its entry in `#changes` on the landing page (`docs/index.html`).

## [0.2.0] — 2026-11-02

### English

Kladde now brings everything HealthLog can store from Health Connect, from every app: trainings, weight, blood pressure and more, sleep from any sleep app, and daily totals as Health Connect adds them up.

#### New
- Trainings from every app (Freeletics, the Pebble app, Google Fit …) with sport, time, pause, heart rate, steps and distance; the sections of one Freeletics training arrive as one.
- Readings from every app: weight, body fat, lean mass, blood pressure, blood glucose, oxygen saturation, heart rate variability, respiratory rate, VO₂ max, body temperature and resting heart rate.
- Sleep from every app. Where two apps saw the same night, the first in Kladde's order goes to HealthLog (Pebble app, Sleep as Android, Google Fit); a night goes four hours after it ended, so the watch's is in.
- App order for sleep and trainings under More: move an app up or down, the upper one wins where two saw the same night or training.
- Active energy and floors climbed as daily totals.

#### Improved
- Daily totals (steps, distance, energy, floors) and heart rate now come from every app, added up by Health Connect in the app order set there, so phone and watch never count twice.
- "Allow more kinds" on More asks for the new kinds; one left out does not stop the sync.

#### Note
- After the update tap "Allow more kinds" on More, so Kladde may read trainings, weight and the other new kinds. The first sync then reads 30 days again; daily totals are replaced, nothing counts twice.
- Daily totals count only the apps in Health Connect's priority list (Health Connect → Activity → Data sources and priority). If distance is missing, add the app that records it, for example Google Fit, and tap "Read everything again".
- A changed app order applies to nights and trainings that begin after the change; those sent before stay as they were.
- The energy of a training does not arrive yet.
- A reading written by two apps, for example the same weighing, arrives twice; readings have no app order yet.

[All changes](https://github.com/shrippen/Kladde/compare/v0.1.0...v0.2.0)

### Deutsch

Kladde bringt jetzt alles aus Health Connect, was HealthLog speichern kann, von jeder App: Training, Gewicht, Blutdruck und mehr, Schlaf aus jeder Schlaf-App und Tagessummen so, wie Health Connect sie zusammenzählt.

#### Neu
- Training von jeder App (Freeletics, Pebble-App, Google Fit …) mit Sportart, Zeit, Pause, Puls, Schritten und Strecke; die Abschnitte eines Freeletics-Trainings kommen als eines an.
- Einzelwerte von jeder App: Gewicht, Körperfett, Magermasse, Blutdruck, Blutzucker, Sauerstoffsättigung, Herzfrequenzvariabilität, Atemfrequenz, VO₂max, Körpertemperatur und Ruhepuls.
- Schlaf von jeder App. Sahen zwei Apps dieselbe Nacht, geht die erste in Kladdes Reihenfolge an HealthLog (Pebble-App, Sleep as Android, Google Fit); eine Nacht geht vier Stunden nach ihrem Ende raus, damit die der Uhr da ist.
- App-Reihenfolge für Schlaf und Training unter „Mehr“: eine App hoch- oder runterschieben, die obere gewinnt, wenn zwei dieselbe Nacht oder dasselbe Training sahen.
- Aktive Energie und Stockwerke als Tagessummen.

#### Verbessert
- Tagessummen (Schritte, Strecke, Energie, Stockwerke) und Puls kommen jetzt von jeder App, zusammengezählt von Health Connect in der dort eingestellten App-Reihenfolge, damit Telefon und Uhr nie doppelt zählen.
- „Weitere Arten erlauben“ unter „Mehr“ fragt nach den neuen Arten; eine nicht erlaubte hält den Abgleich nicht auf.

#### Hinweis
- Nach dem Update unter „Mehr“ auf „Weitere Arten erlauben“ tippen, damit Kladde Training, Gewicht und die anderen neuen Arten lesen darf. Der erste Abgleich liest dann 30 Tage neu; Tagessummen werden ersetzt, nichts zählt doppelt.
- Tagessummen zählen nur die Apps in Health Connects Prioritätsliste (Health Connect → Aktivität → Datenquellen und Priorität). Fehlt die Strecke, die App aufnehmen, die sie aufzeichnet, zum Beispiel Google Fit, und „Alles neu lesen“ tippen.
- Eine geänderte App-Reihenfolge gilt für Nächte und Trainings, die nach der Änderung beginnen; vorher gesendete bleiben, wie sie sind.
- Die Energie eines Trainings kommt noch nicht an.
- Einen Einzelwert, den zwei Apps schreiben, zum Beispiel dieselbe Wägung, bekommt HealthLog doppelt; für Einzelwerte gibt es noch keine App-Reihenfolge.

[Alle Änderungen](https://github.com/shrippen/Kladde/compare/v0.1.0...v0.2.0)

## [0.1.0] — 2026-11-01

### English

The first release: Kladde brings what your Pebble writes to Health Connect into your own HealthLog, in the background, and shows your values on the phone.

#### New
- Sign in to your HealthLog instance like the iOS app: the server is checked first, then password with a second factor (TOTP or recovery code) or single sign-on in the browser.
- Watch sync from Health Connect: steps as daily totals, distance, heart rate in 10-minute blocks, resting heart rate and sleep with its stages, only from the Pebble app, so nothing counts twice.
- Runs in the background (every 60 minutes, from 15 on), reads 30 days back on the first run and then only what changed; "Sync now" and "Read everything again" by hand.
- Nothing gets lost offline: what could not be sent waits in a queue and goes out later, in order and without duplicates.
- Status: last run, entries waiting, errors, and a warning when the watch has sent a kind of data for two days no more.
- Home with the latest value of each metric and its last seven days, History from a week to a year with scale and figures, Log for weight, blood pressure, pulse, body fat and mood.
- German and English.

#### Note
- Needs Android 14 or newer (Health Connect is part of the system) on a 64-bit ARM phone, and HealthLog 1.46 or newer.
- Your values show up in HealthLog with the source "Apple Health": HealthLog does not know Health Connect as a source yet.
- Kladde keeps only a renewable sign-in, encrypted with a key in the Android Keystore; the background sync uses the same sign-in, no extra token.
- Plasma Mobile and reading the watch directly come later.

[All changes](https://github.com/shrippen/Kladde/commits/v0.1.0)

### Deutsch

Das erste Release: Kladde bringt, was deine Pebble nach Health Connect schreibt, im Hintergrund in dein eigenes HealthLog und zeigt deine Werte auf dem Telefon.

#### Neu
- Anmelden an deiner HealthLog-Instanz wie mit der iOS-App: erst wird der Server geprüft, dann Passwort mit zweitem Faktor (TOTP oder Wiederherstellungscode) oder Single Sign-on im Browser.
- Abgleich der Uhr aus Health Connect: Schritte als Tagessummen, Strecke, Herzfrequenz in 10-Minuten-Blöcken, Ruhepuls und Schlaf mit seinen Phasen, nur aus der Pebble-App, damit nichts doppelt zählt.
- Läuft im Hintergrund (alle 60 Minuten, ab 15 möglich), liest beim ersten Lauf 30 Tage zurück und danach nur, was sich geändert hat; „Jetzt abgleichen“ und „Alles neu lesen“ von Hand.
- Offline geht nichts verloren: was nicht gesendet werden konnte, wartet in einer Warteschlange und geht später raus, der Reihe nach und ohne Duplikate.
- Status: letzter Lauf, wartende Einträge, Fehler und eine Warnung, wenn die Uhr eine Art Daten seit zwei Tagen nicht mehr geschickt hat.
- Start mit dem neuesten Wert jeder Messgröße und ihren letzten sieben Tagen, Verlauf von einer Woche bis zu einem Jahr mit Skala und Kennzahlen, Erfassen für Gewicht, Blutdruck, Puls, Körperfett und Stimmung.
- Deutsch und Englisch.

#### Hinweis
- Braucht Android 14 oder neuer (Health Connect gehört zum System) auf einem Telefon mit 64-Bit-ARM und HealthLog 1.46 oder neuer.
- Deine Werte erscheinen in HealthLog mit der Quelle „Apple Health“: HealthLog kennt Health Connect noch nicht als Quelle.
- Kladde behält nur eine erneuerbare Anmeldung, verschlüsselt mit einem Schlüssel im Android Keystore; der Abgleich im Hintergrund nutzt dieselbe Anmeldung, kein zusätzliches Token.
- Plasma Mobile und das direkte Lesen der Uhr kommen später.

[Alle Änderungen](https://github.com/shrippen/Kladde/commits/v0.1.0)
