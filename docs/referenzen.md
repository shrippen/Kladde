# Referenzen

## HealthLog (Server)

- Repo: https://github.com/MBombeck/HealthLog (PolyForm Noncommercial 1.0.0; nur Referenz,
  kein Code übernehmen). Doku: https://docs.healthlog.dev
- Eigene Instanz: `https://hl.arianw.de` (v1.46.2 am 1.11.2026, `GET /api/version`).
- API-Vertrag: `docs/api/openapi.yaml` im HealthLog-Repo.

Was Kladde davon nutzt:

| Endpunkt | Zweck |
|---|---|
| `POST /api/measurements/batch` | Upload, ≤ 500 Einträge, `Idempotency-Key` (24 h), Status je Eintrag |
| `GET /api/auth/me` | Feature-Flags (`features.*`), Zeitzone des Kontos |
| `GET /api/measurements/series`, `/series-batch` | Verläufe für die Anzeige |
| `GET /api/sync/changes` | Delta-Feed (Anzeige-Cache, später) |

Der Batch-Endpunkt spricht HealthKit-Bezeichner (`hkIdentifier`, Werte in Apples Einheiten).
Die `externalId` hat vier Formen:

- Einzelwert: eine stabile Id (bei uns die Health-Connect-Record-Id); der erste Wert gewinnt.
- `stats:<HKQuantityTypeIdentifier>:<YYYY-MM-DD>`: Tagessumme (Schritte, Distanz, Energie,
  Schlafdauer); ein erneuter Upload überschreibt.
- `stats:HKQuantityTypeIdentifierHeartRate:<10-Minuten-Grenze in UTC, z. B. 2026-06-21T14:10:00.000Z>`:
  Durchschnitt mit `valueMin`/`valueMax`; überschreibt.
- `stats:<id>:<YYYY-MM-DD>T<HH>`: Stundensumme; für Brücken-Tokens nicht erlaubt.

Mit einem `measurements:write`-Token schreibt der Server die Quelle `EXTERNAL` und weist jeden
Eintrag ab, der selbst eine `source` nennt (422 `measurement.batch.source_not_permitted`).

## Pebble (Core Devices)

- Offizielle App: https://github.com/coredevices/mobileapp, **GPL-3.0**, Kotlin Multiplatform
  mit Compose Multiplatform. Enthält libpebble3.
- Health-Connect-Sync der App: `pebble/src/commonMain/kotlin/coredevices/pebble/health/PlatformHealthSync.kt`
  (über `com.viktormykhailiv.kmp.health`). Schreibt Steps, HeartRate, Sleep und auf Android
  Exercise mit Distanz (`ExerciseWriteTypes.kt`). Liest nichts.
- Gesundheitsdaten in libpebble3: `libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/health/`
  (`HealthDataParser.kt`, `HealthRecords.kt`), Speicherung in Room (`HealthDao.kt`,
  `HealthDataEntity.kt`), Protokoll `packets/HealthSync.kt`, `services/HealthService.kt`.
  Das ist die Vorlage für die Datenquelle auf Plasma Mobile.
- Bekannter Fehler: https://github.com/coredevices/mobileapp/issues/469 (App 1.15, PebbleOS
  4.41.2: in Health Connect nur Sleep und Exercise, keine Steps und kein HeartRate).
- Hilfe zum Health-Connect-Schalter: https://help.repebble.com/en/articles/15178103-health-tracking

## Pebble auf Linux

- Rockpool (Sailfish): https://github.com/abranson/rockpool. Daemon `libpebble3d` = libpebble3
  als GraalVM Native Image (nur 64 Bit), D-Bus zur Oberfläche, Gesundheitsdaten in Room.
  Lizenz im Repo nicht angegeben (Stand 1.11.2026); vor einer Übernahme klären.
- Forum: https://forum.sailfishos.org/t/rockpool-thread/25596 (seit PebbleOS 4.42 melden
  Nutzer, dass Schritte und Herzfrequenz nicht mehr synchronisieren).
- Gadgetbridge in Waydroid scheidet aus: Waydroid hat keinen Bluetooth-Zugriff.

## Health Connect

- Jetpack-Bibliothek `androidx.health.connect:connect-client`.
- Berechtigungen: `android.permission.health.READ_STEPS`, `READ_HEART_RATE`, `READ_SLEEP`,
  `READ_EXERCISE`, `READ_DISTANCE`, dazu `READ_HEALTH_DATA_HISTORY` und
  `READ_HEALTH_DATA_IN_BACKGROUND`.
- Schlafphasen (`SleepSessionRecord.STAGE_TYPE_*`): 1 Wach, 2 Schlaf, 3 außerhalb des Betts,
  4 Leicht, 5 Tief, 6 REM, 7 wach im Bett.
