# Code-Audit Kladde, 26.11.2026, Stand 925fe3c

## Urteil
Kladde bleibt wartbar. Die Schichten halten: Die Oberfläche spricht nie selbst mit dem Server, und der C++-Kern kommt ohne Grafikbibliothek aus. Seit dem letzten Audit ist der Aufbau aufgeschrieben (`ARCHITECTURE.md`), und die Abweichungen stehen dort. Behoben ist keiner der acht offenen Funde. Der Code hat sich kaum geändert. Neu ist nur eine einmalige Umstellung der Tagessummen in der Quelle, die Tagessummen und Puls liest. Diese Quelle ist dadurch von 274 auf 318 Zeilen gewachsen und rechnet jetzt einen Teil doppelt. Insgesamt steht das Repo gleich da wie beim letzten Audit, mit einer neuen kleinen Kopie.

## Kennzahlen
| | letztes Audit | jetzt |
|---|---|---|
| Quelldateien | 107 | 115 (das Prüfwerkzeug zählt jetzt auch die 8 Gradle-Skripte mit) |
| Einträge in der Bestandsliste | 16 | 16 |
| größte Datei (Zeilen) | 613 (Test der Anmeldung; größte Programmdatei 472) | 613 (unverändert; größte Programmdatei 472) |
| längste Funktion (Zeilen) | 154 (Start der App) | 154 (unverändert) |

## Funde

### 1. Die vier Android-Quellen holen ihre Daten jede auf eigenem, kopiertem Weg aus Health Connect
Kladde liest aus Health Connect vier Arten von Daten: Tagessummen mit Puls, Einzelwerte, Schlaf und Training. Jede dieser „Quellen“ hat ihre eigene Kopie von drei Abläufen:
- Sie fragt, was sich seit dem letzten Mal geändert hat, und blättert dabei Seite für Seite.
- Sie liest alle Datensätze eines Zeitraums seitenweise.
- Sie merkt sich, wie weit sie gelesen hat (den „Cursor“).

Der Lauf, der alle Quellen nacheinander aufruft, hat für jede Quelle eine eigene Schnittstelle und einen eigenen, gleich gebauten Block, der Fehler abfängt. Seit dem letzten Audit ist der Abstand gewachsen. Der Cursor der Tagessummen trägt jetzt zusätzlich eine Kennzeichnung, für welche Fassung der Abbildung er gilt. Der gleich gebaute Cursor der Einzelwerte trägt sie nicht.

Eine neue Datenart heißt also: alles noch einmal kopieren. Im Plan stehen schon „Training: Energie“ und gelöschte Sitzungen. Ein Fehler im Blättern muss viermal gefunden und behoben werden. `ARCHITECTURE.md` verlangt diese Abläufe einmal geschrieben und führt die Kopien als bekannte Abweichung.

Vorschlag: Die drei Abläufe kommen in einen gemeinsamen Baustein, und der Lauf ruft alle Quellen über eine gemeinsame Form auf. Aufwand mittel. Risiko mittel: Tagessummen und Schlaf haben Tests gegen ein nachgebautes Health Connect, Einzelwerte und Training nicht; diese Tests kommen zuerst.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/HealthConnectSource.kt:86`, `…/SleepSource.kt:44`, `…/WorkoutSource.kt:47`, `…/ReadingSource.kt:52` (Änderungen lesen); `…/HealthConnectSource.kt:202`, `…/SleepSource.kt:82`, `…/WorkoutSource.kt:108`, `…/ReadingSource.kt:77` (seitenweise lesen); `…/HealthConnectSource.kt:67`, `…/HealthConnectSource.kt:215`, `…/ReadingSource.kt:41`, `…/ReadingSource.kt:88` (Cursor); `…/SyncRunner.kt:24-38`, `…/SyncRunner.kt:52-93` (Lauf).

### 2. Die Quelle der Tagessummen übersetzt nebenbei die Datensätze aller anderen Quellen
Health Connect liefert Datensätze, Kladde rechnet mit eigenen Messwerten. Die Übersetzung von einem ins andere steht für alle 16 Arten von Datensätzen in einer einzigen Tabelle: Gewicht, Blutdruck, Schlafphasen und so weiter. Diese Tabelle liegt in der Quelle für Tagessummen und Puls, deren Name nach „die Quelle für Health Connect“ klingt. Die Quellen für Einzelwerte, Schlaf und Training rufen sie dort auf, und an derselben Stelle holen sich auch Schlaf und Training die Kennung der Pebble-App.

Wer eine neue Art von Einzelwert einbaut, ändert also die Datei der Tagessummen und muss dafür ihre 318 Zeilen lesen. Am Namen ist nicht zu erkennen, dass die Tabelle dort liegt. Auch die Tabelle in `ARCHITECTURE.md`, wohin Neues gehört, nennt diese Datei nicht.

Vorschlag: Die Übersetzung und die Kennung der Pebble-App kommen in eine eigene kleine Datei, und die Quelle der Tagessummen bekommt einen Namen, der sagt, was sie liest. Aufwand klein. Risiko gering: Reine Verschiebung, und die Abbildung ist getestet.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/HealthConnectSource.kt:238`, `…/HealthConnectSource.kt:276-316`, `…/ReadingSource.kt:58`, `…/ReadingSource.kt:81`, `…/SleepSource.kt:74`, `…/SleepSource.kt:102`, `…/WorkoutSource.kt:80`, `…/WorkoutMapper.kt:24`.

### 3. Die neue Umstellung der Tagessummen rechnet die Tagessumme ein zweites Mal
Version 0.2.0 hat Tagessummen einen Tag zu spät datiert. Deshalb liest die neue Umstellung einmalig alle Tage seit November 2025 noch einmal. Dabei macht sie aus der Antwort von Health Connect auf eigenem Weg Tagessummen: eigene Schleife, eigene Kennung je Tag, eigene Zeitspanne. Daneben steht die gewöhnliche Rechnung, die dasselbe für das normale Zeitfenster tut, mit einer kleinen Abweichung: Nur sie kürzt den heutigen Tag auf „jetzt“.

Wer an der Tagessumme etwas ändert, zum Beispiel die Kennung je Tag oder eine neue Summe, muss beide Stellen finden. Ändert er nur eine, schickt die Umstellung Summen, die der Server nicht als dieselben erkennt. Dazu verweist ein Kommentar auf `migrationDays`, das es im Code nicht gibt. Und es steht nirgends, wann die Umstellung wieder entfallen kann.

Vorschlag: Beide Wege nutzen dieselbe kleine Funktion, die aus einer Antwort von Health Connect Tagessummen macht. Der Kommentar wird berichtigt, und im Plan steht, ab welcher Version die Umstellung wegfällt. Aufwand klein. Risiko gering: Beide Wege haben Tests.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/HealthConnectSource.kt:65-66`, `…/HealthConnectSource.kt:131-137`, `…/HealthConnectSource.kt:176-182`, `…/HealthConnectSource.kt:245-248`, `android/sync/src/test/java/de/arianw/kladde/sync/HealthConnectSourceTest.kt:94`.

### 4. Ob eine Bewegung „gut“ ist, entscheiden drei Stellen in zwei Schichten, ohne Test
Startseite und Verlauf zeigen bei jedem Wert, ob seine Bewegung gut oder schlecht ist. Die Übersetzung von „Trend“ und „Bewertung des Servers“ in diese Angabe steht zweimal kopiert in der Schicht, die der Oberfläche Daten liefert. Sagt der Server nichts, entscheidet eine dritte Stelle in der Oberfläche selbst, zum Beispiel „Schritte: mehr ist gut, Puls: weniger ist gut“.

Außerdem rechnet der Verlauf für Blutdruck selbst den Mittelwert, den tiefsten und den höchsten Wert und die untere Grenze der Skala. `ARCHITECTURE.md` verlangt solche Regeln im Kern, mit Test. Kein Test prüft diese Zahlen; der Test der Ansichten prüft nur die Ladeanzeige.

Wer die Regel für „gut“ ändert, muss drei Stellen in zwei Sprachen finden.

Vorschlag: Regel und Rechnung kommen als je eine Funktion in den Kern (`core/api`), wo die anderen Rechenregeln schon getestet sind. Startseite, Verlauf und Oberfläche nutzen dieselbe Funktion. Aufwand klein. Risiko mittel, solange die Tests fehlen; sie kommen zuerst.

Beleg: `app/dashboard.cpp:47-50`, `app/history.cpp:154-157`, `app/history.cpp:113-147`, `app/qml/Metrics.js:26`, `app/qml/HomeView.qml:90-91`, `app/qml/HistoryView.qml:123`, `tests/test_views.cpp:90`, `tests/test_views.cpp:108`.

### 5. Der C++-Kern hat zwei Wege zum Server, die Fehler verschieden lesen und bewerten
Im C++-Kern sprechen zwei Bausteine mit HealthLog. Der eine ist die Anmeldung, die die App benutzt. Der andere ist ein einfacher Client, den nur das Werkzeug `kladde-push` benutzt. Beide holen den Fehlertext aus der Antwort des Servers, mit kopiertem Code. Beide bauen ihre Anfragen selbst, und nur die Anmeldung setzt ein Zeitlimit.

Auch die Regel, wann sich ein neuer Versuch lohnt, ist verschieden:
- Die Server-Antwort 409 (Konflikt) heißt beim einfachen Client „endgültig“, beim Upload auf Android „gleich noch einmal“.
- Der Kommentar des Clients nennt jede 4xx-Antwort außer 429 endgültig. `agent.md` sagt, nur 422 ist endgültig.

Die Abfrage der Serverversion im einfachen Client ruft niemand auf. `ARCHITECTURE.md` verlangt genau einen Weg, und wer die Fehlerbehandlung ändert, weiß nicht, welche Regel gilt.

Vorschlag: Der einfache Client nutzt das Lesen der Antwort und den Aufbau der Anfrage der Anmeldung mit und folgt bei neuen Versuchen der Regel von Android. Die unbenutzte Funktion fällt weg. Aufwand klein. Risiko gering: Beide haben Tests.

Beleg: `core/healthlogclient.cpp:12-15`, `core/healthlogclient.cpp:22-28`, `core/healthlogclient.cpp:75`, `core/healthlogclient.h:26-27`, `core/session.cpp:35-48`, `android/sync/src/main/java/de/arianw/kladde/sync/Uploader.kt:131-132`.

### 6. Zwei Aufzählungen werden von Hand gleich gehalten
Die App führt zwei Listen selbst: die Zustände der Anmeldung und die Arten eines Eintrags (Gewicht, Blutdruck …). Sie wandelt sie über die Position in die gleichnamige Liste des Kerns um, und ein Kommentar sagt „The same order as kladde::EntryKind“. Fügt jemand im Kern einen Zustand oder eine Art ein oder stellt sie um, bekommt er keine Warnung. Die App zeigt dann still den falschen Zustand oder speichert die falsche Art.

Vorschlag: Die App gibt die Listen des Kerns direkt an die Oberfläche weiter. Die Anmeldung im Kern ist dafür schon vorbereitet. Aufwand klein. Risiko gering: Die Anmeldung ist getestet, der Baustein der App zum Eintragen nicht.

Beleg: `app/account.h:30`, `app/account.cpp:68`, `core/session.h:47-48`, `app/entry.h:21-22`, `app/entry.cpp:17`, `core/api.h:70`.

### 7. Die echte Warteschlange auf dem Telefon hat keinen Test; geprüft wird nur ein nachgebauter Ersatz
Was noch nicht hochgeladen ist, wartet auf dem Telefon in einer kleinen Datenbank, der Warteschlange. Ihre Regeln stehen in `agent.md`: streng der Reihe nach, und ein ersetzter Stapel behält seinen Platz. Alle Upload-Tests laufen aber gegen einen Ersatz im Arbeitsspeicher, der diese Regeln noch einmal von Hand nachbaut. Ob die echte Datenbank sich genauso verhält, prüft kein Test. Jede Änderung an der Warteschlange muss deshalb an zwei Stellen gleich gemacht werden, und nur eine davon wird geprüft.

Vorschlag: Dieselben Tests laufen auch gegen die echte Datenbank, auf dem Gerät oder in einer Testumgebung, die Android auf dem Rechner nachbildet. Aufwand mittel. Risiko gering, denn es kommen nur Tests dazu.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/SqliteQueueStore.kt:118`, `android/sync/src/test/java/de/arianw/kladde/sync/MemoryStore.kt:6`, `…/MemoryStore.kt:57`, `android/sync/src/androidTest/java/de/arianw/kladde/sync/PermissionRequestTest.kt`.

### 8. Der Start der App ist mit dem Demo-Modus verwoben
Die Startfunktion der App ist 154 Zeilen lang. Sie hat 11 Abschnitte, die nur unter bestimmten Bedingungen mitgebaut werden: für Android, für den Demo-Modus oder für beides. Den größten Teil machen das Laden der Demo-Welt und der Ablauf der Bildschirmfotos aus. `ARCHITECTURE.md` verlangt, dass die App den Demo-Code an einer Stelle aufruft. Wer den Start ändert, muss alle Fälle im Kopf behalten.

Vorschlag: Alles, was nur der Demo-Modus braucht, kommt in eine eigene Funktion unter `app/demo/`, die der Start einmal aufruft. Aufwand klein. Risiko gering: Die Bildschirmfotos prüfen den Demo-Modus, und ein Test stellt sicher, dass kein Demo-Code in der Auslieferung steckt.

Beleg: `app/main.cpp:35`, `app/main.cpp:101`, `app/main.cpp:141`.

### 9. Ein Rest in der Demo-Welt, der nichts mehr tut
Die Demo-Welt beantwortet noch die Anfrage nach einem eigenen Upload-Schlüssel. Seit der Entscheidung vom 1.11.2026 stellt die App diese Anfrage nie, und ein Test prüft sogar, dass sie nie kommt. Wer dem Rest glaubt, sucht nach einer Anfrage, die es nicht mehr gibt.

Vorschlag: Die Demo-Antwort fällt weg. Aufwand klein. Risiko keins.

Beleg: `app/demo/demobackend.cpp:252`, `tests/test_account.cpp:122`.

### 10. Bestandsliste: drei Einträge messen weiter falsch, drei trennen sich an einer klaren Stelle
Die Bestandsliste (`structure.baseline`) zählt die Funktionen auf, die heute über den Grenzen für Länge, Verzweigungen oder Verschachtelung liegen. Das Prüfwerkzeug ist seit dem letzten Audit erneuert worden, misst Kotlin aber noch immer falsch. Betroffen sind Funktionen, deren Inhalt nach einem Gleichheitszeichen statt in geschweiften Klammern steht, und Funktionen ganz ohne Inhalt:
- Die Sendefunktion des Uploads soll 88 Zeilen lang sein und hat 5.
- Die Lesefunktion des Laufs hat gar keinen Inhalt; sie ist nur eine Schnittstelle.
- Das Ersetzen eines Stapels hat 10 flache Zeilen; beim Ersatz in den Tests ist es genauso.

Das gehört im Prüfwerkzeug im Repo `regeln` behoben, nicht hier.

An einer natürlichen Stelle trennen sich drei Einträge:
- der Leser des Austauschformats: eine Kette von Fällen je Datentyp, besser als Tabelle; er gehört zur eingefrorenen C++-Seite und wartet mit ihr;
- der Verlauf (Fund 4);
- der Start der App (Fund 8).

In Ordnung, wie sie sind, sind die Übersetzung der Sportarten (eine flache Tabelle), die Antworten der Demo-Welt (eine flache Liste von Adressen) und das Hauptprogramm von `kladde-push`.

Aufwand klein je Eintrag. Risiko gering.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/Uploader.kt:87-91`, `…/SyncRunner.kt:24-38`, `…/SqliteQueueStore.kt:118-127`, `android/sync/src/test/java/de/arianw/kladde/sync/MemoryStore.kt:57`, `core/hcjson.cpp:55`, `android/sync/src/main/java/de/arianw/kladde/sync/WorkoutMapper.kt:89`, `app/demo/demobackend.cpp:237`, `tools/kladde-push.cpp`.

## Seit dem letzten Audit
Behoben ist keiner der Funde. Offen und hier wieder gemeldet sind die alten Funde 1 (jetzt 1), 2 (jetzt 7), 3 (jetzt 5), 5 (jetzt 4, erweitert um die dritte Stelle in der Oberfläche), 6, 7 (jetzt 8), 8 (jetzt 9) und 9 (jetzt 10). Neu sind die Funde 2 und 3.

Fund 4 (die C++-Abbildung) hat der Eigentümer entschieden: Die C++-Seite ist bis Plasma Mobile eingefroren, und so steht es in `agent.md` und `ARCHITECTURE.md`. Er wird nicht neu gemeldet. Eine Beobachtung dazu: PR #34 hat die C++-Seite trotzdem mitgeändert, weil beide Seiten gegen dieselbe Beispieldatei prüfen.

Seit dem letzten Audit dazugekommen ist `ARCHITECTURE.md`. Sie führt die offenen Funde als bekannte Abweichungen und legt fest, wohin Neues gehört.
