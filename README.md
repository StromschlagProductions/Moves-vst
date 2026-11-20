# MOVES – Audio Drama Presence Simulator

<img src="Assets/logo.png" alt="Stromschlag Audio" height="64">

VST3-Effekt für Hörspiel-Produktion, der ganz ohne Eingangssignal auskommt. Er erzeugt selbstständig die
fast unhörbaren, unwillkürlichen Bewegungsgeräusche eines anwesenden Menschen: minimale Gewichtsverlagerungen,
dezentes Kleidungsrascheln, sehr selten ein leiser Atemhauch – als dünne, unregelmäßige Folge einzelner
Ereignisse, nicht als Dauergeräusch.

**⬇ Download (Windows · VST3):** https://github.com/StromschlagProductions/Moves-vst/releases/latest

## Warum es dieses Plugin gibt

Bei Hörspielproduktionen werden oft nur Sprache und bewusst gesetzte Foley-Geräusche verwendet. Besonders
ruhige Dialogszenen können dadurch unnatürlich steril wirken, weil die akustische Mikropräsenz einer
anwesenden Person fehlt – die kleinen, unwillkürlichen Geräusche, die niemand bewusst setzen würde, die aber
im echten Leben immer da sind.

MOVES legt genau diese Ebene unter die Szene. Es ist kein Foley-Sampler für deutlich wahrnehmbare Aktionen
(Schritte, Türen, Gegenstände) und kein Geräuschgenerator im klassischen Sinn, sondern ein sehr zurückhaltender
Präsenz-Simulator. Den besten Eindruck von seiner Wirkung bekommt man, wenn man ihn in einer laufenden Szene
kurz ausschaltet.

– Lars Eickstädt, Stromschlag Audio

## Oberfläche

Oben links Logo und Name, rechts Look Dunkel/Hell und die Hilfe (**?**). Darunter eine einzige Karte mit den
beiden Reglern. Look und Fenstergröße (50–300 %) werden mit dem Projekt gespeichert.

## Regler (automatisierbar)

| Regler | Wirkung |
|---|---|
| **MOVEMENT** | Aktivität der simulierten Person. Niedrig: seltene Ereignisse, lange Ruhephasen, sehr dezent. Mittel: natürlich wirkende, gelegentliche Bewegungen – passend für die meisten Dialogszenen. Hoch: häufigere, lebhaftere Präsenz, aber immer noch eine Folge einzelner Ereignisse statt eines Dauerrauschens. |
| **LEVEL** | Gesamtlautstärke der erzeugten Geräusche, −60 bis +12 dB, feinstufig im unteren Bereich für sehr leise Hintergründe. |

Ein kleiner Teil der Ereignisse (deutlich seltener als Bewegungen, ohne regelmäßigen Atemrhythmus) ist ein
sehr leiser Atemhauch oder ein Nasengeräusch.

## Anwendung

1. MOVES auf eine eigene, leere Spur legen – kein Eingangssignal, keine MIDI-Noten nötig.
2. MOVEMENT passend zur Szene wählen (ruhiges Zweiergespräch: niedrig bis mittel; unruhigere Szene: höher).
3. LEVEL so leise einstellen, dass die Spur erst beim Abschalten auffällt.
4. Für mehrere Figuren ggf. mehrere Instanzen auf getrennten Spuren verwenden (jede erzeugt ihre eigene,
   unabhängige Ereignisfolge).

## Hinweise

- MOVES hat keinen Eingang: Die Spur läuft komplett über den Effekt, es gibt nichts durchzureichen. Im Bypass
  ist MOVES stumm.
- **Reproduzierbar:** Bei gleicher Projektposition und gleichen Reglerwerten entsteht immer dieselbe
  Ereignisfolge – ein zweiter Export derselben Szene klingt also nicht zufällig anders, auch nach einem Sprung
  mitten in die Szene oder bei Loop-Wiedergabe. Voraussetzung ist eine während der Wiedergabe gleichbleibende
  Abtastrate.
- Die Samplebibliothek ist fest eingebaut; es ist keine manuelle Ordnerverwaltung nötig. Details zur Herkunft
  und automatischen Aufbereitung der Aufnahmen: [docs/SAMPLES.md](docs/SAMPLES.md).

## Installation (Windows)

1. Über den Download-Link oben `Moves-VST3-Windows.zip` bzw. die Release-ZIP laden.
2. ZIP entpacken und den **ganzen Ordner** `MOVES.vst3` nach `C:\Program Files\Common Files\VST3` kopieren.
3. DAW neu starten bzw. Plugins neu scannen. Primär getestete Zielumgebung: Studio One Pro 8 unter Windows.

## Unterstützte Plattformen und Formate

Windows, VST3. 44,1 kHz und 48 kHz werden unterstützt; höhere Abtastraten laufen technisch mit, waren aber
keine Entwicklungspriorität für diese Version.

## Build-Anleitung

Voraussetzungen: CMake ≥ 3.22, ein C++17-Compiler, Internetzugang beim ersten Konfigurieren (JUCE wird per
FetchContent geladen, Version 8.0.9).

```
cmake -B build
cmake --build build --config Release
```

Nur die Klang-Engine bauen und testen (ohne JUCE, z. B. für schnelle Regressionstests unter Linux):

```
cmake -B build-dsp -DAUDIO_DSP_ONLY=ON
cmake --build build-dsp
ctest --test-dir build-dsp --output-on-failure
```

## Versionen

Die Versionsnummer steht in der Datei `VERSION`, im Plugin-Kopf und im Namen der Release. Versionsschema und
Release-Ablauf: [docs/VERSIONING.md](docs/VERSIONING.md).

- **1.0** – Erste Version. [Release-Hinweise](docs/releases/1.0.md).

## Aufbau

- `Source/MovesDSP.h`: Klang-Engine (reines C++). Positions-basierte, deterministische Ereignisplanung
  (Zell-/Hash-Raster statt fortlaufendem Zufallsgenerator) – siehe Kommentar am Dateianfang für die
  Reproduzierbarkeits-Begründung.
- `Source/PluginProcessor.*`, `Source/PluginEditor.*`: JUCE-Plugin, Samplebibliothek-Laden und Oberfläche.
- `Assets/samples/movements`, `Assets/samples/breaths_sniffs`: eingebettete Samplebibliothek, siehe
  [docs/SAMPLES.md](docs/SAMPLES.md).
- `.github/workflows/tests.yml`: Linux-DSP-Tests (ASan/UBSan) bei jedem Push.
- `.github/workflows/build.yml`: Windows-Bau, Regressionstests und pluginval; Releases nur nach explizitem
  Workflow-Start mit `publish`.

## Qualität und Prüfung

- **Automatische Tests der Klangberechnung** (`Tests/DSPTests.cpp`, ohne JUCE): keine Audio-Thread-Allokationen,
  Reproduzierbarkeit bei gleicher Position/gleichen Parametern unabhängig von der Blockgröße, Positions-
  Unabhängigkeit (ein direkter Sprung mitten in die Szene liefert dasselbe Ergebnis wie durchgehendes
  Abspielen), endliche/begrenzte Ausgabe auch bei Extremwerten, stummes und stabiles Verhalten ohne
  Samplebibliothek.
- **Prozessor-Tests** (`Tests/ProcessorTests.cpp`, mit JUCE): verschiedene Abtastraten/Blockgrößen/Bus-Layouts,
  Blockaufteilung ändert das Ergebnis nicht, Zustand speichern/wiederherstellen (inkl. defekter Daten),
  GUI-Lebenszyklus, mehrere gleichzeitige Instanzen.
- **Echtzeitsicher:** Während der Wiedergabe wird kein Speicher angefordert; alle Samples werden beim Laden des
  Plugins einmal dekodiert, nicht im Audio-Thread.

### Bekannte Einschränkungen

- **pluginval und Studio One wurden in dieser Entwicklungsumgebung nicht ausgeführt** (kein Windows/VST3-Host
  verfügbar). Der Windows-Build und pluginval Stufe 8 laufen automatisch in `build.yml`; der eigentliche
  Praxistest in Studio One steht noch aus und ist Aufgabe des Autors vor der ersten produktiven Nutzung.
- Die Samplebibliothek wurde automatisch aus den Rohaufnahmen segmentiert, aber noch nicht durchgehört; siehe
  [docs/SAMPLES.md](docs/SAMPLES.md) für die automatisch markierten Kandidaten zur Kontrolle.
- Ohne eine vom Host gemeldete Projektposition (z. B. manche Standalone-Testumgebungen ohne Transport) fällt
  MOVES auf einen intern ab 0 fortlaufend gezählten Ersatzwert zurück; die Reproduzierbarkeits-Garantie gilt
  dann weiterhin innerhalb eines Durchlaufs, aber nicht zwingend positionsgenau zum Host-Transport.
- Kein Airwindows-Code wird verwendet: Die Signalverarbeitung beschränkt sich auf Sample-Wiedergabe mit
  kubischer Interpolation, kurzen Fades und einer einfachen Pegelglättung – dafür war eine eigene, transparente
  Lösung einfacher und ebenso klanglich ausreichend als die Einbindung eines externen Algorithmus.

## Lizenz und verwendete Drittkomponenten

Gebaut mit [JUCE](https://juce.com) 8 (siehe JUCE-Lizenzbedingungen für die gewählte Build-Konfiguration). Die
Samplebibliothek sind eigene Aufnahmen des Autors; Software-Lizenz und Nutzungsrechte an den Aufnahmen sind
getrennt zu betrachten, siehe [docs/SAMPLES.md](docs/SAMPLES.md).

---

## Hinweis / Disclaimer

**Open Source · Work in Progress**

Dieses Plugin wurde in erster Linie für den persönlichen Workflow des Autors bei der Hörspielproduktion
entwickelt. Es ist auf diese eigenen Anforderungen zugeschnitten und muss daher nicht für jeden Anwendungsfall
geeignet sein.

Das Projekt wird laufend weiterentwickelt. Trotz sorgfältiger Tests können Fehler oder Kompatibilitätsprobleme
auftreten. Die Software wird ohne Gewährleistung bereitgestellt; die Nutzung erfolgt auf eigene Verantwortung.
Eine Haftung für mögliche Schäden oder Datenverluste wird, soweit gesetzlich zulässig, ausgeschlossen.

**Feedback, Fehlermeldungen und Verbesserungsvorschläge sind herzlich willkommen!**

Öffentliches Versionsschema und Release-Ablauf: [Versionsnummern](docs/VERSIONING.md).
