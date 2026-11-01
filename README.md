# Kladde

Bringt Gesundheitsdaten der Pebble in die eigene [HealthLog](https://healthlog.dev)-Instanz:
auf Android aus Health Connect, später auf Plasma Mobile direkt von der Uhr. Eine Qt-/Kirigami-App
im Design System Kante für beide Plattformen.

Seite: https://shrippen.github.io/Kladde/ · Anforderungen in [docs/lastenheft.md](docs/lastenheft.md),
Plan in [docs/plan.md](docs/plan.md), Änderungen in [CHANGELOG.md](CHANGELOG.md).

## Installieren

Die APK aus dem [neuesten Release](https://github.com/shrippen/Kladde/releases/latest) auf dem
Telefon öffnen. Braucht Android 14 oder neuer (64-Bit-ARM), die Pebble-App mit Zugriff auf
Health Connect und eine eigene HealthLog-Instanz ab 1.46. In der App: Server eingeben, anmelden
(Passwort mit zweitem Faktor oder SSO), Health Connect erlauben; der Abgleich läuft dann im
Hintergrund.

## Bauen

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Braucht Qt 6 (Core, Network, Quick, QuickControls2, Test, LinguistTools), QtKeychain und zur
Laufzeit Kirigami. `-DKLADDE_APP=OFF` baut nur Kern, Tests und `kladde-push`.

## Demo

```sh
demo/start.sh [de|en]
```

Baut `build-demo/` mit `-DKLADDE_DEMO=ON`: angemeldet als Mara Weber (Studio Weber, Daten aus
`demo/world.json`, kopiert aus `shrippen.github.io/demo`), kein Server, kein Schlüsselbund.
Nur für Screenshots; ein Test (`no_demo_in_app`) prüft, dass die Demo in normalen Builds fehlt.

## Android

```sh
scripts/build-android.sh debug     # de.arianw.kladde.dev, neben einem Release installierbar
scripts/build-android.sh release   # signiert mit KLADDE_KEYSTORE_PATH & Co., sonst unsigniert
adb install -r dist/android/kladde-debug.apk
```

Installiert, was fehlt (NDK, Qt per aqtinstall, KF6/QtKeychain/OpenSSL). Releases baut und
signiert GitHub Actions (`.github/workflows/release.yml`), Ablauf in [agent.md](agent.md).

## Werkzeuge aus Phase 0

`spikes/hc-probe` zeigt und exportiert, was in Health Connect steht; `build/tools/kladde-push`
lädt so einen Export mit einem `measurements:write`-Token hoch (`--dry-run` zeigt nur die Batches).
Exporte sind echte Gesundheitsdaten: nicht ins Repo (`.gitignore` hält `health-connect-*.json` fern).

## Lizenz

GPL-3.0-or-later
