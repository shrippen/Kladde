# Kladde

Bringt Gesundheitsdaten der Pebble in die eigene [HealthLog](https://healthlog.dev)-Instanz:
auf Android aus Health Connect, später auf Plasma Mobile direkt von der Uhr. Eine Qt-/Kirigami-App
im Design System Kante für beide Plattformen.

Stand: Phase 0 (Machbarkeit). Anforderungen in [docs/lastenheft.md](docs/lastenheft.md), Plan in
[docs/plan.md](docs/plan.md).

## Bauen

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

Braucht Qt 6 (Core, Network, Quick, QuickControls2, Test) und zur Laufzeit Kirigami.
`-DKLADDE_APP=OFF` baut nur Kern, Tests und `kladde-push`.

## Probe bauen

```sh
cd spikes/hc-probe
echo "sdk.dir=$HOME/android-sdk" > local.properties
./gradlew assembleDebug
adb install app/build/outputs/apk/debug/app-debug.apk
```

## Phase 0 ausprobieren

1. Die Probe (`spikes/hc-probe`, APK als Artefakt der GitHub-CI) auf dem Telefon installieren, Zugriff
   erteilen, „Count per type and app“ zeigt, was die Pebble-App nach Health Connect schreibt.
   „Export JSON“ speichert die Daten.
2. In HealthLog ein Token mit dem Scope `measurements:write` anlegen.
3. Hochladen, erst trocken:

   ```sh
   build/tools/kladde-push --dry-run health-connect-7-days.json | less
   KLADDE_TOKEN=… build/tools/kladde-push health-connect-7-days.json
   ```

   Standard: nur Daten der Pebble-App (`--app coredevices.coreapp`), Server `https://hl.arianw.de`.

Exporte sind echte Gesundheitsdaten: nicht ins Repo (`.gitignore` hält `health-connect-*.json` fern).

## Lizenz

GPL-3.0-or-later
