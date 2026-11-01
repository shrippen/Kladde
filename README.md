<p align="center"><img src="docs/icon.svg" alt="Kladde" width="96" height="96"></p>

# Kladde

_Your health data in your own HealthLog_

Kladde brings your health data into your own [HealthLog](https://healthlog.dev) instance, in the background, and shows
your values on the phone. On **Android** the data comes from **Health Connect**; on **Plasma Mobile** (later) it comes
straight from the **Pebble**. One Qt/Kirigami app in the [Kante](https://github.com/shrippen/Kante)
design for both. [Deutsch](docs/README.de.md) · [Landing page](https://shrippen.github.io/Kladde/)

## What it syncs

On Android Kladde reads Health Connect, there only the records of the Pebble app (`coredevices.coreapp`), so nothing
counts twice. A kind the Pebble app does not write stays out: today it writes no distance and no resting heart rate. HealthLog's batch endpoint speaks HealthKit, so the values arrive in Apple's units and with the source
"Apple Health".

| Health Connect | In HealthLog | How |
|---|---|---|
| Steps | Steps | one total per local day, the whole day on every run |
| Distance | Walking + running distance | one total per local day, in metres (if the source writes it) |
| Heart rate | Pulse | 10-minute blocks with average, minimum and maximum |
| Resting heart rate | Resting heart rate | one entry per reading (if the source writes it) |
| Sleep session with stages | Sleep | minutes per stage (awake, core, deep, REM) |

## The app

- **Sign-in** like the iOS app: the server is checked first, then password with a second factor (TOTP or recovery code)
  or single sign-on in the browser. Kladde keeps only a renewable sign-in, encrypted with a key in the Android Keystore.
- **Background sync** every 60 minutes (from 15 on), 30 days back on the first run, then only what changed;
  "Sync now" and "Read everything again" by hand.
- **Queue:** what could not be sent waits and goes out later, strictly in order and without duplicates.
- **Status:** last run, entries waiting, errors, a warning when a kind of data has been silent for two days.
- **Home** (latest value and last seven days per metric), **History** (a week to a year, with scale, legend and figures),
  **Log** (weight, blood pressure, pulse, body fat, mood). English and German.

## How it works (short)

```
app/          Kirigami UI (QML with Kante)
  ↓
core/         HealthLog client (sign-in, values, log), mapping, batches
              (Qt Core + Network only; also kladde-push, later the Pebble daemon)

android/sync  Kotlin, Android only: the background sync from Health Connect
```

On Android the Kotlin module `android/sync` reads Health Connect in a WorkManager job, maps the records like
`core/mapper.cpp` (shared fixtures keep both equal), queues them and sends them to `/api/measurements/batch`.
`core/` maps and sends for `kladde-push` (no queue: it stops at the first error) and, later, the Pebble daemon.
Requirements: [docs/lastenheft.md](docs/lastenheft.md), plan: [docs/plan.md](docs/plan.md).

## Install

The APK from the [latest release](https://github.com/shrippen/Kladde/releases/latest). Needs Android 14 or newer on
a 64-bit ARM phone, the Pebble app writing to Health Connect, and HealthLog 1.46 or newer. In the app: enter the server,
sign in, allow Health Connect.

## Development

```sh
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build   # desktop, tests
scripts/build-android.sh debug     # "Kladde Dev" (de.arianw.kladde.dev), installs next to a release
adb install -r dist/android/kladde-debug.apk
```

- Desktop: Qt 6 (Core, Network, Quick, QuickControls2, Test, LinguistTools), QtKeychain, Kirigami at run time.
  `-DKLADDE_APP=OFF` builds only the core, the tests and `kladde-push`.
- Android: `scripts/build-android.sh` installs what is missing (NDK, Qt via aqtinstall, KF6, QtKeychain, OpenSSL).
- Demo mode for screenshots (no server, demo data "Studio Weber"): `demo/start.sh [de|en]`.
- Releases: built, signed and attached only by GitHub Actions (`.github/workflows/release.yml`); steps in
  [agent.md](agent.md). Changes: [CHANGELOG.md](CHANGELOG.md).
- Phase 0 tools: `spikes/hc-probe` shows and exports what is in Health Connect, `kladde-push` uploads such an export
  (`--dry-run` shows the batches). Exports are real health data: never into the repository.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE), like the Pebble app. HealthLog is PolyForm Noncommercial: Kladde only talks
to its API and contains none of its code.

## Credits

[HealthLog](https://healthlog.dev), the [Pebble app](https://github.com/coredevices/mobileapp) by Core Devices,
[Qt](https://www.qt.io/), [Kirigami](https://invent.kde.org/frameworks/kirigami),
[QtKeychain](https://github.com/frankosterfeld/qtkeychain), [Health Connect](https://developer.android.com/health-and-fitness/guides/health-connect).
