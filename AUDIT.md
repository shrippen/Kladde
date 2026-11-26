# Code-Audit Kladde, 26.11.2026, Stand 0dfe448

## Urteil
Kladde ist gut wartbar und klar besser als beim letzten Audit. Von den zehn Funden sind acht ganz behoben und zwei zum Teil. Die Bestandsliste ist von 16 auf 11 Einträge geschrumpft, und die längste Funktion außerhalb der eingefrorenen C++-Abbildung hat jetzt 80 statt 154 Zeilen. Die Umbauten haben keine großen neuen Kopien gebracht. Zwei Dinge sind aber auf halbem Weg stehen geblieben: Schlaf und Training lesen weiter jeweils mit einer eigenen Kopie desselben Ablaufs. Und neben der echten Warteschlange gibt es weiter einen nachgebauten Ersatz, der von Hand gleich gehalten werden muss. Der wichtigste neue Fund: Die Übersetzung der Health-Connect-Daten, die auf dem Telefon läuft, wird nicht gegen die gemeinsamen Beispieldateien geprüft. Geprüft wird nur ein zweiter Leser, den das Telefon nie benutzt.

## Kennzahlen
| | letztes Audit | jetzt |
|---|---|---|
| Quelldateien | 115 | 127 |
| Einträge in der Bestandsliste | 16 | 11 (mit dem berichtigten Prüfwerkzeug: drei falsch gemessene Kotlin-Einträge sind weg, zwei richtig gemessene sind dazugekommen) |
| größte Datei (Zeilen) | 613 (Test der Anmeldung; größte Programmdatei 472) | 613 (unverändert; größte Programmdatei 435, die Anmeldung im Kern) |
| längste Funktion (Zeilen) | 154 (Start der App) | 135 (Leser des Austauschformats, eingefrorene C++-Seite); der Start der App hat jetzt 80 |

## Funde

### 1. Die Beispieldateien prüfen einen Leser, den das Telefon nie benutzt; die echte Übersetzung hat keinen Test der Werte
Kladde prüft seine Abbildung gegen gemeinsame Beispieldateien, zum Beispiel einen Tag der Pebble mit Gewicht, Blutdruck und Schlaf. Auf Android liest diese Dateien ein eigener Leser des Austauschformats. Er liegt im Programmteil, der mit der App ausgeliefert wird, aber nur die Tests rufen ihn auf. Er wandelt Daten in Kladdes Messwerte um und baut dafür die Schlafregel ein zweites Mal nach: welche Schlafphase als „bekannt“ zählt und dass eine Nacht ohne Phasen ein einziger Block ist. Auf dem Telefon übersetzt aber eine andere Datei die Datensätze von Health Connect. Für ihre 16 Arten mit ihren Einheiten (Kilogramm, mmHg, Grad Celsius …) und für ihre Schlafregel gibt es keinen Test der Werte. Der einzige Test, der durch sie läuft, prüft bei einem Gewicht nur die Kennung.

Das kostet: Ein Fehler in einer Einheit oder in der Schlafregel auf dem Telefon fällt in keinem Test auf. Wer die Schlafregel ändert, muss zwei Stellen finden, und nur die Kopie wird geprüft. Außerdem trägt die App Code mit sich, der nur für Tests da ist. Dieser Leser ist auch der größte Kotlin-Eintrag der Bestandsliste.

Vorschlag: Der Leser kommt zu den Tests. Die Tests der Abbildung bauen aus den Beispieldateien echte Health-Connect-Datensätze und schicken sie durch die echte Übersetzung. Aufwand mittel. Risiko gering, denn es kommen vor allem Tests dazu.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/ExchangeJson.kt:8-12`, `…/ExchangeJson.kt:15-16`, `…/ExchangeJson.kt:119-125`, `…/RecordSamples.kt:26-27`, `…/RecordSamples.kt:61-64`, `android/sync/src/test/java/de/arianw/kladde/sync/MapperTest.kt:28`, `…/MapperTest.kt:43`, `…/ReadingSourceTest.kt:26`, `…/ReadingSourceTest.kt:36`.

### 2. Die Grenzen der Sync-Einstellungen stehen in drei Schichten, und sie stimmen nicht überein
Wie weit der erste Sync zurückreicht und wie oft er läuft, hat Grenzen und Vorgaben: höchstens 365 Tage, mindestens 15 Minuten, vorgegeben 30 Tage und 60 Minuten. Diese Zahlen stehen einzeln in der Seite, im Baustein der App und im Android-Modul. Dort sind sie nicht gleich: Die Seite und der Baustein der App erlauben höchstens 365 Tage, das Android-Modul 3650. Die einmalige Umstellung der Tagessummen verlässt sich laut ihrem Kommentar darauf, dass es nie mehr als 365 Tage sind.

Wer die Grenze ändert, muss drei Stellen in drei Sprachen finden. Hebt er sie nur auf der Seite an, reicht die Umstellung still nicht mehr weit genug zurück.

Vorschlag: Das Android-Modul hält Grenzen und Vorgaben, und der Baustein der App und die Seite fragen es danach. Aufwand klein. Risiko gering.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/KladdeSync.kt:36-38`, `…/KladdeSync.kt:197`, `app/syncbridge.h:128-129`, `app/syncbridge.cpp:50`, `app/syncbridge.cpp:61`, `app/qml/MoreView.qml:148`, `app/qml/MoreView.qml:161`, `android/sync/src/main/java/de/arianw/kladde/sync/DayTotalSource.kt:203-205`.

### 3. Welche Werte Tagessummen sind, steht an fünf Stellen; die Länge eines Pulsblocks an drei
Schritte, Strecke, aktive Energie und Stockwerke werden als Tagessumme hochgeladen. Diese Liste steht an fünf Stellen:
- zweimal in der Abbildung, als Tabelle und noch einmal als Fall in der Rechnung;
- im Baustein, der ganze Tage zuschneidet;
- zweimal in der Quelle der Tagessummen: was Health Connect summiert, und an welchem Tag ein gelöschter Datensatz neu zu rechnen ist.

Auch dass ein Pulsblock 10 Minuten lang ist, steht an drei Stellen, einmal als nackte Zahl 600. Die Funktion, die das Ende eines Pulsblocks rechnet, fragt jedes Mal nach der Art des Werts, bekommt im Programm aber immer „Puls“.

Eine neue Tagessumme heißt also: fünf Stellen finden. `ARCHITECTURE.md` nennt davon nur die Abbildung. Wer den Fall in der Rechnung vergisst, bekommt einen Absturz beim Lesen, und keine Tagessumme kommt mehr an. Wer die Stelle für gelöschte Datensätze vergisst, rechnet nach dem Löschen den falschen Tag neu.

Vorschlag: Die Liste der Tagessummen und die Länge eines Pulsblocks werden je einmal festgelegt, und alle Stellen leiten sich daraus ab. Die überflüssige Angabe der Art fällt weg. Aufwand klein. Risiko gering: Abbildung und Quelle sind getestet.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/Mapper.kt:41-46`, `…/Mapper.kt:79`, `…/DayWindow.kt:23`, `…/DayTotalSource.kt:190-200`, `…/DayTotalSource.kt:210-219`; Pulsblock: `…/Mapper.kt:31`, `…/DayTotalSource.kt:208`, `…/DayWindow.kt:34`; Art immer Puls: `…/DayWindow.kt:29`, `…/DayTotalSource.kt:163`; `ARCHITECTURE.md:79`.

### 4. Schlaf und Training lesen weiter jeweils mit einer eigenen Kopie desselben Ablaufs
Seit dem Umbau stehen das Abfragen der Änderungen und das seitenweise Lesen an einer Stelle. Darüber liegt aber ein zweiter Ablauf, den Schlaf und Training Zeile für Zeile gleich gebaut haben:
- das erste Lesen über das ganze Zeitfenster;
- aus den Änderungen die betroffenen Tage bestimmen;
- einen Tag früher anfangen, weil eine Nacht oder ein Training über Mitternacht geht;
- Zurückgehaltenes nachholen und aus mehreren Apps eine auswählen;
- die Apps auflisten, die etwas geschrieben haben.

Die beiden Kopien unterscheiden sich nur in der Art des Datensatzes, der Wartezeit und dem, was am Ende daraus wird. Selbst die Wartezeit liegt an verschiedenen Orten: beim Schlaf in der Quelle, beim Training in der Abbildung. Dazu legt `ARCHITECTURE.md` fest, dass jede Quelle die Merkhilfe für ihren Lesestand („Cursor“) gemeinsam nutzt. Schlaf und Training speichern aber den rohen Stand ohne diese Merkhilfe. Das ist für die Handys, die ihn schon gespeichert haben, richtig so, steht aber nirgends.

Eine Änderung an der Regel für „zurückgehalten“, an der Wahl der Tage oder eine dritte Art von Sitzung muss also an zwei oder drei Stellen gleich gemacht werden.

Vorschlag: Ein gemeinsamer Baustein für das Lesen von Sitzungen, dem Schlaf und Training nur Art, Wartezeit und Umwandlung mitgeben. `ARCHITECTURE.md` sagt, dass diese zwei Quellen den rohen Stand speichern. Aufwand klein. Risiko gering: Beide Quellen haben Tests gegen ein nachgebautes Health Connect.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/SleepSource.kt:34-66`, `…/WorkoutSource.kt:37-69`, `…/SleepSource.kt:69-70`, `…/WorkoutSource.kt:94-95`, `…/SleepSource.kt:81`, `…/WorkoutMapper.kt:18`, `ARCHITECTURE.md:64`.

### 5. Der nachgebaute Ersatz der Warteschlange bleibt und prüft nur noch sich selbst
Seit Fund 7 laufen die Upload-Tests gegen die echte Warteschlange auf dem Telefon, und das ist gut so. Aber sie laufen zusätzlich weiter gegen den Ersatz im Arbeitsspeicher. Er baut dieselben Regeln auf 87 Zeilen von Hand nach. Nichts anderes braucht ihn mehr. Die Regeln der Warteschlange stehen also weiter an zwei Stellen, und jede Änderung muss in beiden gleich gemacht werden. Sonst schlägt ein Test des Ersatzes fehl, obwohl die echte Warteschlange stimmt.

Nebenbei: In diesen Upload-Tests stecken auch alle Tests des ganzen Sync-Laufs. Am Namen ist das nicht zu erkennen.

Vorschlag: Der Ersatz und seine Testklasse fallen weg, und die Tests laufen nur gegen die echte Warteschlange. Die Tests des Laufs bekommen eine eigene Datei. Aufwand klein. Risiko keins: Die echte Warteschlange hat dieselben Tests.

Beleg: `android/sync/src/test/java/de/arianw/kladde/sync/MemoryStore.kt:6-7`, `…/UploaderMemoryTest.kt:4-5`, `…/UploaderSqliteTest.kt:12-17`, `…/UploaderTest.kt:17-19`, `…/UploaderTest.kt:60`, `…/UploaderTest.kt:313`.

### 6. Der Start der App entscheidet noch an sechs Stellen selbst über den Demo-Modus
Fund 8 hat die Demo-Welt und die Bildschirmfotos aus dem Start der App herausgeholt. Das ist ein klarer Gewinn. Sechs Weichen sind aber geblieben: eigener Name der Einstellungsdatei, eigene Symbole, die Sprache für die Fotos, ein Schlüsselbund nur im Arbeitsspeicher, keine Erneuerung der Anmeldung und kein Wiederherstellen der letzten Anmeldung. `ARCHITECTURE.md` sagt, die App ruft den Demo-Code an genau einer Stelle auf, und führt die Abweichung seit dem Umbau nicht mehr. Die Startfunktion hat genau 80 Zeilen, also die Grenze der Prüfung. Die nächste Zeile bringt sie zurück auf die Bestandsliste.

Wer den Start ändert, muss weiter jede dieser Weichen im Kopf behalten.

Vorschlag: Was der Demo-Modus anders macht, kommt in eine Funktion unter `app/demo/`, die der Start einmal aufruft. Alternativ nennt `ARCHITECTURE.md` diese Weichen wieder als Abweichung. Aufwand klein. Risiko gering: Die Bildschirmfotos prüfen den Demo-Modus, ein Test prüft, dass kein Demo-Code in der Auslieferung steckt.

Beleg: `app/main.cpp:47`, `app/main.cpp:57`, `app/main.cpp:91`, `app/main.cpp:108`, `app/main.cpp:119`, `app/main.cpp:153`, `app/main.cpp:79` (Länge 80), `ARCHITECTURE.md:73`.

### 7. Die Seite zum Eintragen führt die Felder jeder Art noch einmal selbst
Der Kern weiß, welche Felder ein Eintrag hat und welche Grenzen gelten, zum Beispiel beim Blutdruck „systolic“, „diastolic“ und, wenn man will, „pulse“. Die Seite zum Eintragen schreibt dieselben Feldnamen noch einmal auf, mit Beschriftung und Einheit. Weichen die Namen ab, meldet der Kern das Feld als fehlend, und der Eintrag lässt sich nie speichern, ohne Fehlermeldung. Eine ähnliche kleine Kopie: Dass bei Schritten keine Einheit erscheint, entscheiden Startseite und Verlauf je für sich.

Vorschlag: Der Baustein der App gibt der Seite die Felder aus dem Kern weiter, die Seite ergänzt nur Beschriftung und Einheit. Aufwand klein. Risiko gering: Der Kern ist getestet, die Seite nicht.

Beleg: `app/qml/LogView.qml:16-28`, `core/api.cpp:211-229`, `app/entry.cpp:50-58`, `app/qml/HomeView.qml:85`, `app/qml/HistoryView.qml:16`.

### 8. Zwei Reste, die nichts bewirken
- Der Upload zählt, wie viele Einträge der Server neu angelegt hat, und liest das dafür aus jeder Antwort. Niemand verwendet diese Zahl.
- Das Werkzeug `kladde-push` fragt den Server standardmäßig nach der Zeitzone des Kontos. Es verlangt aber selbst einen Upload-Schlüssel, und mit so einem Schlüssel verweigert der Server diese Frage immer (`agent.md`, Gelernte Fehler). Das Werkzeug nimmt dann jedes Mal die Zeitzone des Rechners.

Wer solchen Code liest, sucht nach einem Zweck, den es nicht gibt.

Vorschlag: Die Zählung fällt weg. `kladde-push` nimmt ohne Angabe gleich die Zeitzone des Rechners, und die Abfrage im Client fällt weg. Aufwand klein. Risiko gering: Beide haben Tests.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/Uploader.kt:33`, `…/Uploader.kt:55`, `…/Uploader.kt:125-129`; `tools/kladde-push.cpp:7`, `tools/kladde-push.cpp:133-143`, `core/healthlogclient.h:43-44`, `core/healthlogclient.cpp:91-106`.

### 9. Einige Kommentare und eine Zeile in `ARCHITECTURE.md` sagen etwas anderes als der Code
- Der Kern sagt, Blutdruck gehe als zwei Anfragen. Der Baustein der App schickt ihn absichtlich als eine einzige, damit er ganz oder gar nicht gespeichert wird.
- Im Sync-Lauf steht die Erklärung, wie Lesefehler behandelt werden, über einer Funktion, die etwas anderes tut. Die Skizze des Laufs zeigt den Schlaf nicht.
- `ARCHITECTURE.md` schickt den, der eine neue Datenart einbaut, zur Liste der Berechtigungen im Android-Modul. Diese Liste entsteht aber von selbst aus den Arten, die jede Quelle liest; ändern muss man die Quelle.

Wer diesen Texten glaubt, ändert an der falschen Stelle.

Vorschlag: Die Texte werden berichtigt. Aufwand klein. Risiko keins.

Beleg: `core/api.h:91-92`, `app/entry.h:13-15`, `android/sync/src/main/java/de/arianw/kladde/sync/SyncRunner.kt:12-15`, `…/SyncRunner.kt:107-112`, `ARCHITECTURE.md:79`, `android/sync/src/main/java/de/arianw/kladde/sync/KladdeSync.kt:42-51`.

### 10. Bestandsliste: zwei Einträge trennen sich an einer klaren Stelle, der Rest ist in Ordnung
Seit das Prüfwerkzeug Kotlin richtig misst, sind die drei falschen Einträge weg. Die Liste stimmt jetzt.

An einer klaren Stelle trennen sich:
- der Leser des Austauschformats auf Android: Er geht mit Fund 1 zu den Tests;
- der Sync-Lauf: ein gleich gebauter Block je Quelle. Das ist der bewusst offene Teil von Fund 1, er steht als Abweichung in `ARCHITECTURE.md` und wird hier nicht neu gemeldet.

In Ordnung, wie sie sind:
- die Übersetzung der Health-Connect-Datensätze und die der Sportarten: je eine flache Tabelle;
- die Antworten der Demo-Welt: eine flache Liste von Adressen;
- der nachgebaute Server im Test der Anmeldung;
- das Hauptprogramm von `kladde-push`;
- der Prototyp in `spikes/`.

Der Leser des Austauschformats und die Abbildung in C++ gehören zur eingefrorenen Seite und warten auf Plasma Mobile.

Aufwand klein. Risiko gering.

Beleg: `android/sync/src/main/java/de/arianw/kladde/sync/ExchangeJson.kt:41`, `…/SyncRunner.kt:41`, `…/RecordSamples.kt:29`, `…/WorkoutMapper.kt:89`, `app/demo/demobackend.cpp`, `tests/test_session.cpp`, `tools/kladde-push.cpp:25`, `core/hcjson.cpp:55`, `core/mapper.cpp:97`.

## Seit dem letzten Audit
Der Eigentümer hat alle zehn Funde zur Behebung gewählt.

- **Fund 1 (Lesen aus Health Connect), zum Teil behoben (PR #44, #45).** Änderungen abfragen, seitenweise lesen und der Lesestand stehen jetzt einmal, für alle Quellen. Bewusst offen bleibt der Sync-Lauf mit einem Block je Quelle; er steht als Abweichung in `ARCHITECTURE.md` und wird nicht neu gemeldet. Neu sichtbar geworden ist die Kopie eine Ebene darüber bei Schlaf und Training (jetzt Fund 4).
- **Fund 2 (Übersetzung in der Quelle der Tagessummen), behoben (PR #41).** Die Übersetzung hat eine eigene Datei, und die Quelle heißt nach dem, was sie liest.
- **Fund 3 (Umstellung rechnet doppelt), behoben (PR #36).** Beide Wege nutzen dieselbe Funktion. Der Kommentar ist berichtigt, und der Plan sagt, dass die Umstellung mit 0.4 entfällt.
- **Fund 4 (Regel für „gut“), behoben (PR #40).** Die Regel und die Rechnung für den Verlauf liegen im Kern und haben Tests. Die Oberfläche entscheidet nicht mehr selbst.
- **Fund 5 (zwei Wege zum Server), behoben (PR #39).** Beide nutzen denselben Aufbau der Anfrage und dasselbe Lesen der Antwort. Die Regel für neue Versuche ist auf beiden Seiten gleich, die unbenutzte Funktion ist weg.
- **Fund 6 (Aufzählungen von Hand), behoben (PR #42).**
- **Fund 7 (Warteschlange ungetestet), behoben (PR #38).** Der nachgebaute Ersatz ist aber geblieben (jetzt Fund 5).
- **Fund 8 (Start mit Demo verwoben), zum Teil behoben (PR #43).** Demo-Welt und Bildschirmfotos sind ausgelagert, sechs Weichen sind geblieben (jetzt Fund 6).
- **Fund 9 (Demo-Rest), behoben (PR #37).**
- **Fund 10 (Prüfwerkzeug misst Kotlin falsch), behoben (regeln#20, PR #46).**

Die Entscheidung des Eigentümers, die C++-Seite der Abbildung bis Plasma Mobile einzufrieren, gilt weiter. Sie wird nicht neu gemeldet.
