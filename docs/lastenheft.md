# Lastenheft Kladde

Stand: 1. November 2026

## 1. Ausgangslage und Zweck

Die Gesundheitsdaten der Pebble (Schritte, Herzfrequenz, Schlaf, Workouts) sollen automatisch,
vollständig und ohne Duplikate in der eigenen HealthLog-Instanz (`https://hl.arianw.de`) landen.

- HealthLog ist ein selbst gehosteter Server. Für iOS gibt es eine native App mit
  HealthKit-Sync, für Android nur die PWA und einen manuellen Datei-Import aus Health Connect
  (`docs/integrations/health-connect.md` im HealthLog-Repo). Diese Lücke schließt Kladde.
- Die Pebble hängt heute an einem Android-Telefon. Die Core-Devices-App schreibt dort Steps,
  HeartRate, Sleep und Exercise (mit Distanz) nach Health Connect (siehe
  [referenzen.md](referenzen.md)).
- Perspektivisch soll die Pebble an einem Plasma-Mobile-Telefon hängen. Dort gibt es kein
  Health Connect; die Daten kommen dann direkt von der Uhr über einen libpebble3-Daemon.

Kladde ist ein Client: Er speichert keine eigene Gesundheitsdatenbank und rechnet keine eigenen
Auswertungen, das bleibt HealthLog.

## 2. Ziele

| Prio | Ziel |
|---|---|
| Muss | Health-Connect-Daten (vor allem die der Pebble) laufen automatisch und inkrementell nach HealthLog |
| Muss | Eine Codebasis (Qt 6, Kirigami, Kante) für Android und Plasma Mobile |
| Muss (später) | Auf Plasma Mobile kommen die Pebble-Daten direkt von der Uhr (Datenquelle über D-Bus) |
| Soll | Dashboard und Verläufe der wichtigsten Werte aus HealthLog, auf beiden Plattformen |
| Soll | Schnelle manuelle Einträge (Gewicht, Blutdruck, Stimmung) |
| Kann | Rückschreiben nach Health Connect (Gewicht, Blutdruck, Glukose) |

## 3. Einsatz

- **Nutzer:** eine Person, eigene HealthLog-Instanz (ab v1.46), erreichbar per HTTPS.
- **Plattformen:** Android 14 und neuer (Health Connect im System), Plasma Mobile (aarch64),
  Plasma Desktop für die Entwicklung.

## 4. Funktionale Anforderungen

### Verbindung

- **LF-01** Serveradresse und Token eingeben. Phase 1: ein HealthLog-API-Token mit dem Scope
  `measurements:write` (eine Brücke, die nicht das Telefon ist; Werte landen als Quelle
  `EXTERNAL`). Später, wenn HealthLog es anbietet: Anmeldung wie die iOS-App mit eigener
  Quelle `HEALTH_CONNECT` (siehe Plan, Upstream).
- **LF-02** Tokens liegen im Android Keystore bzw. in KWallet / Secret Service (QtKeychain), nie
  in einer Klartextdatei.
- **LF-03** Kann: TLS-Zertifikat-Pinning.

### Datenquellen

Jede Quelle liefert dieselben neutralen Datensätze (`core/record.h`); Abbildung, Warteschlange
und Upload sind für alle gleich.

- **LF-10** Health Connect (Android): Berechtigungen je Datentyp anfordern, mit der
  Rationale-Activity, die Health Connect verlangt.
- **LF-11** Erstimport der Historie (`READ_HEALTH_DATA_HISTORY`, sonst liefert Health Connect nur
  30 Tage vor der ersten Freigabe).
- **LF-12** Danach inkrementell über die Changes-API mit Changes-Token; Löschungen werden
  übernommen, soweit HealthLog das für die Quelle erlaubt.
- **LF-13** Hintergrund-Sync per WorkManager (`READ_HEALTH_DATA_IN_BACKGROUND`), Intervall
  einstellbar (Standard 1 h), dazu „Jetzt synchronisieren“.
- **LF-14** Datentypen, Muss: Steps, HeartRate, SleepSession mit Phasen, ExerciseSession,
  Distance. Kann: RestingHeartRate, ActiveCaloriesBurned, Weight, BloodPressure, BloodGlucose,
  OxygenSaturation, HeartRateVariabilityRmssd.
- **LF-15** Quellenfilter nach App (Standard: nur `coredevices.coreapp`), damit nichts doppelt
  zählt.
- **LF-16** Pebble-Daemon (Plasma Mobile, später): dieselben Typen, über die D-Bus-Schnittstelle
  des Daemons.

### Upload

- **LF-20** Upload über `POST /api/measurements/batch`, höchstens 500 Einträge je Aufruf, mit
  `Idempotency-Key`.
- **LF-21** Stabile `externalId` je Eintrag: Health-Connect-Record-ID bzw. die
  `stats:`-Formen für Tagessummen und 10-Minuten-Herzfrequenzblöcke. Ein wiederholter Upload
  erzeugt keine Duplikate.
- **LF-22** Offline-Warteschlange mit Wiederholung und Backoff. Ein 422 mit Fehlern je Eintrag
  wirft nur die genannten Einträge aus, der Rest geht erneut.
- **LF-23** Der Fortschritt (Changes-Token, Stand der Warteschlange) rückt erst nach bestätigtem
  Upload vor.
- **LF-24** Statusseite: letzter Sync, Anzahl je Typ, Fehler, und eine Warnung, wenn eine Quelle
  einen Typ länger nicht mehr liefert (Pebble-Fehler wie coredevices/mobileapp#469).

### Anzeige

- **LF-30** Heute: Schritte, Ruhepuls, Schlaf der letzten Nacht, Trend gegen 30 Tage.
- **LF-31** Verlauf je Metrik (Tag, Woche, Monat, Jahr) aus `/api/measurements/series` bzw.
  `/api/measurements/series-batch`; Diagramme mit Achsenbeschriftung, Legende und Tooltips.
- **LF-32** Schlaf als Hypnogramm (Wach, REM, Leicht, Tief).
- **LF-33** Manuelle Einträge über die HealthLog-API.

## 5. Nicht-funktionale Anforderungen

- **NF-01 Leistung:** kein Polling im Vordergrund, nur inkrementell. Herzfrequenz geht als
  10-Minuten-Blöcke (rund 144 Einträge pro Tag), nicht als Einzelwerte. Der Erstimport eines
  Jahres läuft seitenweise ohne Speicherspitzen. Gemessen werden Akkuverbrauch pro Tag und
  Dauer des Erstimports.
- **NF-02 Datenschutz:** Daten gehen nur an den eigenen Server. Keine Analytics, keine
  Crash-Dienste Dritter. Lokal liegt nur die Warteschlange.
- **NF-03 Robustheit:** Abbruch an beliebiger Stelle ohne Datenverlust (LF-23).
- **NF-04 Design:** Kante-QML-Modul, unverändert kopiert (`tools/sync-kante.sh`). Keine hart
  kodierten Farben oder Schriften; fehlende Bausteine (Hypnogramm) kommen nach Kante.
- **NF-05 Sprachen:** Deutsch und Englisch.
- **NF-06 Demo-Modus:** Daten aus der Studio-Weber-Welt (`shrippen.github.io/demo/world/`, dafür
  um Gesundheitsdaten erweitern). Nie in einem Release; der Release-Build prüft das und bricht ab.
- **NF-07 Tests und CI:** Abbildung, Batches und Upload-Logik laufen ohne Gerät (Fixtures aus
  echten Health-Connect-Daten). Was Health Connect oder eine Uhr braucht, bleibt lokal.

## 6. Abgrenzung

- Keine eigene Auswertung neben HealthLog (Health Score, Coach, Medikamente, Dokumente bleiben in
  der Weboberfläche).
- Kein Ersatz für die Pebble-App auf Android (Watchfaces, Benachrichtigungen).
- Die Begleit-App für die Pebble auf Plasma Mobile (Kopplung, Benachrichtigungen, Firmware) ist
  ein eigenes Projekt; Kladde liest nur deren Gesundheitsdaten.
- Keine Google-Health-Cloud-Anbindung (die hat HealthLog serverseitig).
- Kein iOS-Build.

## 7. Abnahme

1. Ein Pebble-Tag (Schritte, Herzfrequenz, Schlaf) steht spätestens 1 h später korrekt in
   HealthLog, gegengeprüft mit dem Health-Tab der Pebble-App.
2. Zehn Syncs nacheinander erzeugen null Duplikate.
3. Flugmodus mitten im Sync: kein Datenverlust, der Rest kommt beim nächsten Lauf.
4. Dieselbe App läuft auf Plasma Mobile und zeigt das Dashboard aus HealthLog.
5. Das Release-APK enthält keinen Demo-Code; der Build bricht sonst ab.
