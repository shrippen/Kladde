# Vorbild: die HealthLog-iOS-App

Stand: 1. November 2026, gelesen aus github.com/MBombeck/healthlog-iOS. Lizenz PolyForm
Noncommercial 1.0.0: Kladde übernimmt Aufbau, Abläufe und die API-Formen des Servers, keinen
Code.

## Navigation

Tab-Leiste: **Home**, **Meds**, **Log** (Aktion in der Mitte, öffnet ein Blatt mit Messwert,
Medikament, Stimmung), **Insights**, **More**. Kladde übernimmt die Reihenfolge; Meds kommt
später (nicht im Lastenheft).

| iOS | Kladde (Kirigami) | Lastenheft |
|---|---|---|
| Onboarding: Server-URL, Anmeldung, HealthKit, Benachrichtigungen | Einrichtung: Server, Anmeldung, Health Connect | LF-01, LF-10 |
| Home: Heute-Kopf, Kacheln mit Wert, Einheit, Sparkline, Trend; Sync-Status | Home | LF-30, LF-24 |
| Log: Messwert-Blatt (zuletzt genutzte Arten), Stimmung | Log-Aktion | LF-33 |
| Insights: Übersicht plus eine Seite je Messart; Diagramm mit Tag … Jahr, Alle | Verlauf | LF-31, LF-32 |
| More → Einstellungen: Konto, Server & Sync, Diagnose | Mehr | LF-24 |

## Anmeldung

- Server-URL nur https; Prüfung mit `GET /api/auth/registration-status` und `/api/version`.
- Passwort (`POST /api/auth/login`, ggf. MFA über `/api/auth/mfa/verify`) oder Web-Login mit
  PKCE (`/api/auth/native/login` → `POST /api/auth/native/token`), der Passkeys und SSO
  mitbringt.
- Access- und Refresh-Token; `POST /api/auth/refresh` rotiert den Refresh-Token, immer nur ein
  Refresh zugleich, bei 401 einmal erneuern und wiederholen. Header `X-Client-Type: native`.

## Sync

- Batches ≤ 500 nach `/api/measurements/batch`, ein Idempotency-Key je Batch; 422 nennt
  Einträge, der Rest geht erneut. Wie Kladde.
- Frische je Datentyp berechnet der Server (`GET /api/integrations/healthkit`:
  `metricFreshness`, `health`); die App zeigt sie an.

## Lesen

- Home: `/api/dashboard/summary`, `/api/dashboard/widgets`.
- Diagramme: `/api/measurements/series?kind=…&days=N` (Werte, Einheit, Statistik vom Server),
  `/api/measurements/series-batch`.

## Manuelle Einträge

- `POST /api/measurements` mit `{type, value, measuredAt, notes?}`; Blutdruck als zwei Einträge
  (`BLOOD_PRESSURE_SYS`, `BLOOD_PRESSURE_DIA`). Eingabe in der Einheit des Nutzers, gesendet in
  kg, mmHg, mg/dL. Grenzen wie der Server (z. B. Gewicht 0,1–600 kg, Puls 30–250).
- Stimmung: `POST /api/mood-entries` mit Stufe 1–5 (`LAUSIG` … `SUPER_GUT`), Notiz.
