#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
const char* kThemeProp = "theme";   // 0 = dunkel, 1 = hell (wird mit dem Projekt gespeichert)
const char* kUiWProp = "uiW";       // Fenstergroesse (wird mit dem Projekt gespeichert)
const char* kUiHProp = "uiH";
}

// Farben wie bei den anderen Stromschlag-Audio-Plugins (Schiefer-Gruen + Orange)
MovesEditor::Theme MovesEditor::darkTheme()
{
    return { juce::Colour (0xff182326), juce::Colour (0xff233539), juce::Colour (0xfff5781a),
             juce::Colour (0xffe8ecec), juce::Colour (0xff8fa1a5), juce::Colour (0xff35494e), true };
}
MovesEditor::Theme MovesEditor::lightTheme()
{
    return { juce::Colour (0xffd5dbda), juce::Colour (0xffe8eae9), juce::Colour (0xfff5781a),
             juce::Colour (0xff233539), juce::Colour (0xff5d6e72), juce::Colour (0xffb5bfbe), false };
}

// Inhalt der Onboard-Hilfe, in der Reihenfolge der Oberflaeche
static juce::AttributedString buildHelp (juce::Colour text, juce::Colour accent, juce::Colour dim)
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.setJustification (juce::Justification::topLeft);

    auto head = [&] (const char* t)
    { s.append (u8 (t) + "\n", juce::Font (juce::FontOptions (14.0f, juce::Font::bold)), accent); };
    auto body = [&] (const char* t)
    { s.append (u8 (t) + "\n\n", juce::Font (juce::FontOptions (13.0f)), text); };
    auto note = [&] (const char* t)
    { s.append (u8 (t) + "\n\n", juce::Font (juce::FontOptions (12.0f)), dim); };

    s.append (u8 ("MOVES \xe2\x80\x93 Hilfe") + u8 (" \xc2\xb7 v") + juce::String (PLUGIN_VERSION_TEXT) + "\n\n",
              juce::Font (juce::FontOptions (17.0f, juce::Font::bold)), accent);

    head ("WARUM MOVES");
    body ("Menschen sind nie vollkommen regungslos: kleine Haltungswechsel, Gewichtsverlagerungen, dezentes "
          "Kleidungsrascheln. In ruhigen Dialogszenen fehlt das oft, weil nur Sprache und bewusst gesetzte "
          "Foley-Geräusche verwendet werden \xe2\x80\x93 die Szene wirkt steril. MOVES legt genau diese fast "
          "unhörbare Mikropräsenz darunter. Am besten merkt man die Wirkung erst, wenn man MOVES ausschaltet.");

    head ("EINSATZ");
    body ("MOVES auf eine eigene, leere Spur legen \xe2\x80\x93 kein Eingangssignal, keine MIDI-Noten nötig. "
          "Das Plugin erzeugt selbstständig eine dünne, unregelmäßige Folge von Ereignissen, auch während "
          "Sprechpausen. Mehrere Instanzen (z. B. je eine pro Figur) sind möglich.");

    head ("MOVEMENT");
    body ("Wie aktiv die simulierte Person ist. Niedrig: seltene Ereignisse, lange Ruhephasen. Mittel: "
          "natürlich wirkende, gelegentliche Bewegungen \xe2\x80\x93 für die meisten Dialogszenen passend. "
          "Hoch: häufigere, lebhaftere Präsenz. Auch bei hoher Einstellung bleibt es eine Folge einzelner "
          "Ereignisse, kein Dauerrascheln.");

    head ("LEVEL");
    body ("Gesamtlautstärke der erzeugten Geräusche, feinstufig für sehr leise Hintergründe einstellbar.");

    head ("ATEM-/SCHNIEFGERÄUSCHE");
    body ("Ein kleiner Teil der Ereignisse ist ein sehr leiser Atemhauch oder ein Nasengeräusch \xe2\x80\x93 "
          "deutlich seltener als Bewegungen und ohne regelmäßigen Atemrhythmus.");

    head ("REPRODUZIERBARKEIT");
    body ("Bei gleicher Projektposition und gleichen Reglerwerten erzeugt MOVES immer dieselbe Ereignisfolge "
          "\xe2\x80\x93 ein zweiter Export derselben Szene klingt also nicht zufällig anders.");

    head ("Allgemein");
    body ("Doppelklick auf einen Drehregler oder Strg-Klick setzt ihn auf den Standardwert zurück.");
    body ("Look: Dunkel / Hell umschalten, die Wahl wird mit dem Projekt gespeichert. Das Fenster ist am "
          "rechten unteren Rand skalierbar (50" "\xe2\x80\x93" "300 %).");

    head ("Idee und Konzept");
    body ("Lars Eickstädt, Stromschlag Audio");
    note ("Gebaut mit JUCE. Das Klangmaterial sind eigene, unbearbeitete Aufnahmen des Autors.");

    return s;
}

MovesEditor::MovesEditor (MovesProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    addAndMakeVisible (content);
    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 3, kBaseH * 3);   // 50 % bis 300 %
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);

    logo = juce::ImageCache::getFromMemory (BinaryData::logo_png, BinaryData::logo_pngSize);
    theme = (int) processor.apvts.state.getProperty (kThemeProp, 0) == 1 ? lightTheme() : darkTheme();
    setLookAndFeel (&look);

    setupKnob (movement, "movement", "MOVEMENT");
    setupKnob (level, "level", "LEVEL");

    libraryStatus.setJustificationType (juce::Justification::centred);
    libraryStatus.setFont (juce::FontOptions (11.5f));
    content.addAndMakeVisible (libraryStatus);

    themeButton.onClick = [this]
    {
        theme = theme.dark ? lightTheme() : darkTheme();
        processor.apvts.state.setProperty (kThemeProp, theme.dark ? 0 : 1, nullptr);
        applyTheme();
    };
    content.addAndMakeVisible (themeButton);

    helpButton.setButtonText ("?");
    helpButton.setTooltip (u8 ("Hilfe: was machen die Regler?"));
    helpButton.onClick = [this] { helpScroll.setVisible (! helpScroll.isVisible()); };
    content.addAndMakeVisible (helpButton);

    helpScroll.setViewedComponent (&helpText, false);
    helpScroll.setScrollBarsShown (true, false);
    helpScroll.setVisible (false);
    content.addChildComponent (helpScroll);   // startet ausgeblendet

    movement.slider.setTooltip (u8 ("Aktivit\xc3\xa4t der simulierten Person: niedrig = selten/ruhig, hoch = h\xc3\xa4ufiger/lebhafter."));
    level.slider.setTooltip (u8 ("Gesamtlautst\xc3\xa4rke der erzeugten Ger\xc3\xa4usche."));

    applyTheme();
    updateLibraryStatus();
    startTimerHz (2);   // zeigt die Bibliothek an, sobald sie geladen ist (robust, falls das sehr schnell geht)

    const int savedW = (int) processor.apvts.state.getProperty (kUiWProp, kBaseW);
    const int restoredW = juce::jlimit (kBaseW / 2, kBaseW * 3, savedW);
    setSize (restoredW, juce::roundToInt ((float) restoredW * (float) kBaseH / (float) kBaseW));
    sizeReady = true;
}

MovesEditor::~MovesEditor() { setLookAndFeel (nullptr); }

void MovesEditor::timerCallback() { updateLibraryStatus(); }

void MovesEditor::updateLibraryStatus()
{
    const int nMove = processor.numMovementSamples();
    const int nBreath = processor.numBreathSamples();
    if (nMove == 0 && nBreath == 0)
        libraryStatus.setText (u8 ("Keine Samplebibliothek gefunden \xe2\x80\x93 MOVES bleibt stumm"), juce::dontSendNotification);
    else
        libraryStatus.setText (juce::String (nMove) + u8 (" Bewegungs- und ") + juce::String (nBreath) + u8 (" Atem-/Schniefgeräusche geladen"),
                                juce::dontSendNotification);
}

void MovesEditor::applyTheme()
{
    look.setColour (juce::Slider::thumbColourId, theme.accent);
    look.setColour (juce::Slider::rotarySliderFillColourId, theme.accent);
    look.setColour (juce::Slider::rotarySliderOutlineColourId, theme.line);
    look.setColour (juce::Slider::trackColourId, theme.accent.withAlpha (0.75f));
    look.setColour (juce::Slider::backgroundColourId, theme.line);
    look.setColour (juce::Slider::textBoxTextColourId, theme.text);
    look.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    look.setColour (juce::PopupMenu::backgroundColourId, theme.panel);
    look.setColour (juce::PopupMenu::textColourId, theme.text);
    look.setColour (juce::Label::textColourId, theme.text);
    look.setColour (juce::TextButton::buttonColourId, theme.dark ? theme.panel : theme.bg);
    look.setColour (juce::TextButton::textColourOffId, theme.text);

    movement.label.setColour (juce::Label::textColourId, theme.dim);
    level.label.setColour (juce::Label::textColourId, theme.dim);
    libraryStatus.setColour (juce::Label::textColourId, theme.dim);
    themeButton.setButtonText (theme.dark ? "Look: Dunkel" : "Look: Hell");
    helpText.setAttributed (buildHelp (theme.text, theme.accent, theme.dim), theme.panel);
    helpScroll.fill = theme.panel;
    helpScroll.edge = theme.line;
    sendLookAndFeelChange();
    repaint();
}

void MovesEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text)
{
    // nur senkrecht ziehen (wie bei den meisten Plugins): hoch = mehr, runter = weniger
    k.slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k.slider.setMouseDragSensitivity (180);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    content.addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    content.addAndMakeVisible (k.label);
    k.attachment = std::make_unique<APVTS::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void MovesEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);
    g.addTransform (juce::AffineTransform::scale (uiScale));
    const int W = kBaseW;

    // Kopfleiste
    g.setColour (theme.panel);
    g.fillRect (0, 0, W, 64);
    g.setColour (theme.line);
    g.fillRect (0, 63, W, 1);
    g.setColour (theme.accent);
    g.fillRect (0, 0, 5, 64);

    if (logo.isValid())
    {
        g.setColour (theme.text);   // Logo ist eine Tinten-Maske und wird in der Textfarbe gezeichnet
        g.drawImage (logo, juce::Rectangle<float> (14.0f, 4.0f, 56.0f, 56.0f),
                     juce::RectanglePlacement::centred, true);
    }

    g.setColour (theme.text);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("MOVES", 80, 8, 110, 30, juce::Justification::centredLeft);
    g.setColour (theme.accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("AUDIO DRAMA PRESENCE SIMULATOR", 188, 15, 330, 22, juce::Justification::centredLeft);
    g.setColour (theme.dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (u8 ("STROMSCHLAG AUDIO  \xc2\xb7  v" PLUGIN_VERSION_TEXT), 80, 36, 300, 16, juce::Justification::centredLeft);

    // Kleines Symbol: ein ruhiger Punkt (die Person) mit zwei kaum merklichen Wellen (Mikrobewegung)
    {
        juce::Path dot, waves;
        const float cx = 0.0f, cy = 0.0f;
        dot.addEllipse (cx - 3.5f, cy - 3.5f, 7.0f, 7.0f);
        for (float r : { 7.0f, 12.0f, 17.0f })
            waves.addArc (cx - r, cy - r, 2.0f * r, 2.0f * r,
                          -juce::MathConstants<float>::pi * 0.32f,
                          juce::MathConstants<float>::pi * 0.32f, true);

        juce::GlyphArrangement ga;
        ga.addLineOfText (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)),
                          "AUDIO DRAMA PRESENCE SIMULATOR", 0.0f, 0.0f);
        const float subEnd = 188.0f + ga.getBoundingBox (0, -1, true).getWidth();
        const float helpX = (float) (kBaseW - 170);
        const float mid = 0.5f * (subEnd + helpX);
        const auto placeXf = juce::AffineTransform::scale (1.1f).translated (mid, 25.0f);
        dot.applyTransform (placeXf);
        waves.applyTransform (placeXf);

        const auto ink = theme.panel.interpolatedWith (theme.text, 0.30f);
        const auto fill = theme.panel.interpolatedWith (theme.text, 0.14f);
        g.setColour (fill);
        g.fillPath (dot);
        g.setColour (ink);
        g.strokePath (waves, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Einzige Karte: MOVEMENT und LEVEL
    const float cardX = 12.0f, cardY = 84.0f, cardW = (float) W - 24.0f, cardH = 320.0f;
    g.setColour (theme.panel);
    g.fillRoundedRectangle (cardX, cardY, cardW, cardH, 8.0f);
    g.setColour (theme.line);
    g.drawRoundedRectangle (cardX + 0.5f, cardY + 0.5f, cardW - 1.0f, cardH - 1.0f, 8.0f, 1.0f);
    g.setColour (theme.accent);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("MOVES", (int) cardX + 16, (int) cardY + 14, 200, 16, juce::Justification::centredLeft);
    g.setColour (theme.dim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (u8 ("Audio Drama Presence Simulator"), (int) cardX + 16, (int) cardY + 14, (int) cardW - 32, 16,
                juce::Justification::centredRight);
}

void MovesEditor::resized()
{
    if (sizeReady)
    {
        processor.apvts.state.setProperty (kUiWProp, getWidth(), nullptr);
        processor.apvts.state.setProperty (kUiHProp, getHeight(), nullptr);
    }
    uiScale = std::min ((float) getWidth() / kBaseW, (float) getHeight() / kBaseH);
    content.setTransform (juce::AffineTransform::scale (uiScale));
    content.setBounds (0, 0, kBaseW, kBaseH);
    repaint();

    const int w = kBaseW;
    themeButton.setBounds (w - 170, 18, 112, 28);
    helpButton.setBounds (w - 48, 18, 32, 28);
    helpScroll.setBounds (12, 84, w - 24, 320);
    helpText.setWidth (helpScroll.getMaximumVisibleWidth());

    auto placeKnob = [] (Knob& k, int cx, int y, int cw)
    {
        k.label.setBounds (cx - cw / 2, y, cw, 18);
        k.slider.setBounds (cx - cw / 2 + 10, y + 22, cw - 20, 170);
    };
    placeKnob (movement, w / 2 - 150, 150, 200);
    placeKnob (level, w / 2 + 150, 150, 200);

    libraryStatus.setBounds (24, 364, w - 48, 20);
    helpScroll.toFront (false);
}
