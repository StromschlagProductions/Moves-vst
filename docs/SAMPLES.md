# Die MOVES-Samplebibliothek

## Herkunft

Ausgangsmaterial sind zwei unbearbeitete Rohaufnahmen des Autors (24-Bit, 48 kHz, mono, aus der eigenen
Aufnahmekabine, ohne Hall, ohne Dynamikbearbeitung):

- `Move.wav` – ca. 88 s dezente Körper- und Kleidungsbewegungen
- `Breathe.wav` – ca. 58 s subtile Atem- und Nasengeräusche

Beide Originaldateien sind unverändert erhalten geblieben (sie liegen nicht im Repository, da es sich um
persönliches Aufnahmematerial des Autors handelt; vgl. Lizenzhinweis unten).

## Automatische Vorsegmentierung

Statt blind nach festen Zeitabständen zu schneiden, wurde pro Datei:

1. eine Pegel-Hüllkurve (RMS, 25 ms Fenster, 10 ms Schrittweite) berechnet,
2. ein **lokaler, mitlaufender Hintergrundpegel** bestimmt (gleitendes 15./20.-Perzentil über 1–2,5 s), weil
   beide Aufnahmen kein konstantes Rauschbett haben,
3. mit einer **Hysterese-Schwelle** relativ zu diesem Hintergrund (höhere Einsatz- als Ausklingschwelle)
   jeweils ein zusammenhängendes Ereignis erkannt,
4. kurze Lücken innerhalb eines Ereignisses zusammengeführt, zu lange Ereignisse (> 1,8 s bei Atmern/Schniefern)
   am leisesten Punkt innerhalb des Ereignisses nochmals geteilt, damit kein mehrfacher Atemzyklus als ein
   "Sample" landet,
5. Fragmente, die dabei lediglich Rest-Stille ohne eigenen Pegel waren, verworfen,
6. jedes verbleibende Ereignis mit kurzem Vor-/Nachlauf (Movements: 50/120 ms, Atmer: 60/180 ms) und einem
   kurzen Equal-Power-Fade (10–35 ms) exportiert – **ohne** Pegelnormierung, Rauschunterdrückung oder sonstige
   klangliche Veränderung; einzige Bearbeitung ist der für klickfreie Schnittgrenzen technisch nötige Fade.

Das Werkzeug dafür liegt nicht im Plugin-Repository (es ist ein einmaliges Analyse-Skript, kein Teil der Engine),
die Parameter und Entscheidungen sind hier dokumentiert, damit sie nachvollziehbar und bei neuem Rohmaterial
wiederholbar sind.

## Ergebnis

- **55 Movements** (`Assets/samples/movements/movement_0XX.wav`), 0,3–2,8 s lang
- **37 Breaths/Sniffs** (`Assets/samples/breaths_sniffs/breath_0XX.wav`), 0,3–1,9 s lang

## Bekannte Grenze: automatische Vorauswahl, keine Gehörprüfung

Die Segmentierung arbeitet rein pegelbasiert. 11 der 92 Samples wurden dabei automatisch markiert
(nicht entfernt, nur zur Kontrolle vorgemerkt):

- 7 Movements deutlich lauter als der Median der Kategorie (`movement_012/040/043/044/045/046` und das ohnehin
  schon laute `movement_043`) – vermutlich etwas deutlichere Gewichtsverlagerungen; akustisch plausibel, aber
  nicht geprüft, ob sie im Mix zu auffällig wirken.
- 4 Samples sehr leise (`movement_001`, `breath_003/005/009`, −55 bis −60 dBFS Spitzenpegel) – könnten im
  Projekt kaum hörbar sein.
- `breath_001` ist mit −24 dBFS das lauteste Sample der Bibliothek insgesamt.

**Diese Einschätzung beruht ausschließlich auf Pegel- und Dauerstatistik, nicht auf einer Gehörprüfung** – das
automatische System kann nicht hören, ob ein Sample tatsächlich natürlich klingt, einen störenden Nebenton hat
oder zu auffällig ist. Vor der produktiven Nutzung sollte der Autor die Bibliothek einmal durchhören (am
einfachsten: `Assets/samples/movements` bzw. `.../breaths_sniffs` in der DAW oder einem Player durchgehen) und
ungeeignete Dateien entfernen; die Engine kommt mit einer kleineren Bibliothek ohne weitere Anpassung zurecht.

## Lizenz der Aufnahmen

Die Aufnahmen sind eigenes Audiomaterial des Autors. Die Software-Lizenz von MOVES (siehe README) deckt nicht
automatisch die Nutzungsrechte an den Aufnahmen ab – diese sind getrennt zu betrachten. Eine Weitergabe der
Samplebibliothek mit künftigen MOVES-Releases setzt voraus, dass der Autor dafür eine Lizenz festlegt.

## Interne Aufbereitung ab 1.2

Die WAV-Dateien im Repository bleiben unverändert. Beim Laden des Plugins werden kurze
Impulse mit einer vorausschauenden, geglätteten Pegelhüllkurve abgeschwächt und weiche
Ränder angewendet. Eine begrenzte Pegelangleichung macht leise Bewegungen nutzbarer und
ordnet Atem-/Nasengeräusche darunter ein. Zielwerte und Grenzen stehen in den
[Release-Hinweisen 1.2](releases/1.2.md). Diese Bearbeitung ersetzt keine Hörprüfung
der Aufnahmeinhalte und kann einen ungeeigneten Geräuschtyp nicht passend machen.
