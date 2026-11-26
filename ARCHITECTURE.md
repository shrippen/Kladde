# Architecture

How this repository is meant to be built. Where the code differs today is listed under
"Known deviations". Change this file in the PR that changes the structure.

## What it does

Kladde brings the health data of a Pebble watch into the user's own HealthLog server:
on Android it reads them from Health Connect and uploads them in the background; later,
on Plasma Mobile, straight from the watch. The app shows the latest values and their
history from HealthLog and lets the user enter values by hand.

## Building blocks

| Path | Its one task |
|---|---|
| `core/` | Everything about HealthLog that needs no user interface (Qt Core and Network only): the sign-in and session (`session`, `pkce`, `secretstore`), what HealthLog's answers and bodies look like, the limits of a manual entry and how a value is written (`api`), mapping readings to HealthLog's entries (`sample`, `mapper`), cutting them into batches (`batch`), the exchange format a data source delivers (`hcjson`), and the plain upload client of the command line tool (`healthlogclient`). |
| `app/` (C++) | What the QML side sees, one object per topic: account, home (`dashboard`), history, manual entry, the background sync (`syncbridge`), error texts. Besides that the places for the tokens (desktop keychain, Android vault), a queue for blocking calls and Android's back key (`platform/`). |
| `app/syncbridge*.cpp`, `app/sessionvault_android.cpp` | The two doors between the app and the Android sync module: the bridge for the sync (`_android` calls Kotlin, `_none` is the desktop without sync) and the vault for the tokens, which the module keeps. |
| `app/qml/` | The pages (setup, home, history, log, more) in QML. `Kante/` in it is a synced copy of the design system, never edited here. |
| `app/demo/` | The demo mode: a made-up HealthLog for screenshots. Not in a release build. |
| `android/sync/` | The Android sync module in Kotlin, without Qt: the sources that read Health Connect (day totals with heart rate, single readings, sleep, workouts), the mapping (`Mapper`, `WorkoutMapper`), batches, the queue on disk, the uploader, the background job, the token vault. `KladdeSync` is its one entry for the app. |
| `app/android/`, `android/` (rest) | The Android package of the app; the Gradle build. |
| `tools/kladde-push.cpp` | A command line tool that uploads a file in the exchange format through `core/`. |
| `tests/` | Tests of `core/` and `app/` against a fake server; `fixtures/` with the shared example day. The Kotlin tests lie in `android/sync/src/test/`. |
| `docs/` | Requirements (`lastenheft.md`), plan (`plan.md`), sources, landing page. |
| `demo/`, `scripts/`, `spikes/`, `tools/` (rest) | Demo world and its start; Android build and release notes; the Health Connect probe, kept as record; Kante sync, QML lint, the structure check (synced copy). |

## Layers

Each layer talks only to its neighbour directly below.

```
 app/qml/        pages (QML with Kante)
      │
      ▼
 app/            QML objects: account, home, history, entry, sync
      │                                   │ only on Android: app/syncbridge, token vault
      ▼                                   ▼
 core/           HealthLog: session,   android/sync/  Health Connect ─► mapping ─►
                 reading, mapping,     (Kotlin)       queue ─► upload, in the
                 batches                              background, without the app
      │                                   │
      ▼                                   ▼
            HealthLog server (the user's own)

 tools/kladde-push ─► core/         later: Pebble daemon ─► exchange format ─► core/
```

- QML never speaks HTTP and computes no number it shows; it asks the objects of
  `app/`.
- An object of `app/` translates between QML and `core/`: state, lists, texts. A
  calculation or a rule (a mean, a scale, what counts as good) lives in `core/` and
  has a test.
- `core/` stays without Qt GUI and without QML, so it runs in tests and in
  `kladde-push`. It talks to HealthLog in one way: one place builds a request, reads
  an error and decides whether a retry makes sense.
- A call for the app is in two parts: the object of `app/` names the path and sends
  it through `core/session`; `core/api` knows what the answer and the body look like.
- The app reaches the Android sync module at two places only: `app/syncbridge` for
  the sync, `app/sessionvault_android.cpp` for the tokens, which the module holds. The
  module knows nothing of the app and runs without it.
- In the sync module a source says only what it reads; asking Health Connect for
  changes, reading a range page by page and keeping the cursor are written once and
  used by every source.
- The mapping to HealthLog exists in Kotlin (`Mapper.kt`) and in C++
  (`core/mapper.cpp`). The Kotlin one leads: a change goes there. The C++ one is
  frozen until the work on Plasma Mobile and is then brought up from the Kotlin
  one; `tests/fixtures/pebble-day.expected.json` covers what both do today.
- A list of kinds (sign-in states, kinds of an entry) is defined once, in `core/`,
  and handed on, not copied.
- Demo code lives in `app/demo/` only; the rest of the app calls it at one place.
- Tokens live in the platform's keychain or vault, never in a settings file.

## Where new things go

| Change | Touch |
|---|---|
| A new kind of health data from Health Connect | A source in `android/sync/`, its permission in `app/android/AndroidManifest.xml` and `KladdeSync.READ_PERMISSIONS`, `Sample.kt`, `Mapper.kt`, with tests; the C++ side stays frozen |
| A change to how a value is mapped (unit, day, id) | `Mapper.kt`, with a test; the C++ side stays frozen |
| Something about queueing or uploading on the phone | `android/sync/` (`QueueStore`, `Uploader`, `SyncRunner`), a test |
| A new call to HealthLog for the app | The object of `app/` that sends it through `core/session`; the shape of its answer or body in `core/api`; a test with the fake server |
| Something new on a page | An object in `app/` that offers it, the page in `app/qml/`; a number it needs is computed in `core/` |
| A new kind of manual entry | `core/` (the kind and its limits), `app/entry`, the page |
| A new error text | `app/errors`, translations in `app/i18n/` |
| A new data source besides Health Connect | It delivers the exchange format (`core/hcjson.h`) to `core/` |
| A design element that is missing | Kante, then `tools/sync-kante.sh`; never `app/qml/Kante/` here |
| A requirement or a step of the plan | `docs/lastenheft.md`, `docs/plan.md` |

## Known deviations

- Each of the four Android sources carries its own copy of reading changes, reading
  a range and keeping the cursor (`AUDIT.md`, finding 1).
- The queue on disk has no test; the tests run against a stand-in that rebuilds its
  rules (`AUDIT.md`, finding 2).
- `core/` has two ways to the server, the session and the plain client of
  `kladde-push`, which read and judge errors differently (`AUDIT.md`, finding 3).
- The account reads its two answers (server version, sign-in options) itself, in
  `app/account.cpp`, not in `core/api` (not in `AUDIT.md`).
- Two lists of kinds are kept equal by hand between `app/` and `core/`
  (`AUDIT.md`, finding 6).
- The start of the app is interwoven with the demo mode (`AUDIT.md`, finding 7); the
  sync bridge carries a demo switch of its own (not in `AUDIT.md`); the demo world
  still answers a request the app no longer makes (`AUDIT.md`, finding 8).
