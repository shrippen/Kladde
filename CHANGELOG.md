# Changelog

The release notes of Kladde, newest first. Each section is the text of the release on Gitea and
GitHub and of its entry in `#changes` on the landing page (`docs/index.html`).

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
