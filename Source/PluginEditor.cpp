#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
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
    auto head = [&] (const char* t) { s.append (u8 (t) + "\n", juce::Font (juce::FontOptions (14, juce::Font::bold)), accent); };
    auto body = [&] (const char* t) { s.append (u8 (t) + "\n\n", juce::Font (juce::FontOptions (13)), text); };
    head ("MOVES – Hilfe");
    body ("Dezente Bewegungs- und Atemgeräusche für Hörspielszenen. Auf einer eigenen, leeren Spur verwenden; kein Eingangssignal und keine MIDI-Noten nötig.");
    head ("AKTIVITÄT");
    body ("Niedrig: einzelne Bewegungen mit längeren Pausen. Mittel: regelmäßige, zurückhaltende Präsenz. Hoch: deutlich lebhaftere, überlappende Bewegungen. Atem-/Nasengeräusche bleiben selten. 0 % bedeutet seltene Ereignisse, keine Stummschaltung.");
    head ("LAUTSTÄRKE");
    body ("Ausgangspegel von −60 bis +12 dB. Die eingebauten Geräusche werden intern behutsam im Pegel angeglichen; Atemgeräusche liegen zurückhaltender darunter. −6 dB ist die Grundeinstellung; der Regler normiert nicht die Lautheit der gesamten Szene.");
    head ("ANZEIGE");
    body ("Die Zeitleiste zeigt die Ereignisplanung bei der aktuellen Aktivität: Bewegungen orange, Atem-/Nasengeräusche gedämpft grün. Die senkrechte Marke zeigt die aktuelle Position. Bei Änderungen der Aktivität wird die Vorschau neu berechnet; bereits laufende Geräusche klingen weich aus. Status und Pegelanzeige zeigen die tatsächliche Ausgabe. Die Pegelanzeige ist ein Spitzenpegelmesser, kein LUFS-Messer.");
    head ("WEICHE GERÄUSCHE");
    body ("Kurze Impulse werden beim Laden vorausschauend abgeschwächt. Weiche Ein- und Ausblendungen schützen die Ereignisränder; Lautstärkeänderungen werden geglättet. Es gibt keine zusätzlichen Regler dafür.");
    head ("WIEDERGABE UND PROJEKT");
    body ("Jede neue Instanz hat ihre eigene, im Projekt gespeicherte Ereignisfolge. Bei gleichbleibenden Parametern und gleicher Abtastrate liefern Zeitsprünge und Loops denselben Abschnitt wie durchgehende Wiedergabe. Ohne Hostposition läuft ein interner Zähler. Klangänderungen gegenüber älteren Versionen sind beabsichtigt; wichtige ältere Szenen vorher als Audio sichern.");
    head ("BEDIENUNG");
    body ("Doppelklick oder Strg-Klick setzt einen Regler zurück. Zahlenwerte können direkt eingegeben werden, auch mit Dezimalkomma. Dunkel/Hell und Fenstergröße werden mit dem Projekt gespeichert. Die Oberfläche ist von 50 bis 300 % skalierbar.");
    s.append (u8 ("Idee und Konzept: Lars Eickstädt · Stromschlag Audio"), juce::Font (juce::FontOptions (12)), dim);
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
    theme = processor.uiTheme.load() == 1 ? lightTheme() : darkTheme();
    setLookAndFeel (&look);

    setupKnob (movement, "movement", u8 ("AKTIVITÄT"));
    setupKnob (level, "level", u8 ("LAUTSTÄRKE"));

    content.addAndMakeVisible (events);
    libraryStatus.setJustificationType (juce::Justification::centred);
    libraryStatus.setFont (juce::FontOptions (11.5f));
    content.addAndMakeVisible (libraryStatus);

    themeButton.onClick = [this]
    {
        theme = theme.dark ? lightTheme() : darkTheme();
        processor.uiTheme.store (theme.dark ? 0 : 1);
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
    startTimerHz (20);   // zeigt die Bibliothek an, sobald sie geladen ist (robust, falls das sehr schnell geht)

    const int savedW = processor.uiWidth.load();
    const int restoredW = juce::jlimit (kBaseW / 2, kBaseW * 3, savedW);
    setSize (restoredW, juce::roundToInt ((float) restoredW * (float) kBaseH / (float) kBaseW));
    sizeReady = true;
}

MovesEditor::~MovesEditor() { setLookAndFeel (nullptr); }

void MovesEditor::timerCallback() { events.refresh(); }

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
    events.colours (theme.panel, theme.text, theme.dim, theme.accent, theme.line);
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
    themeButton.setButtonText (theme.dark ? "Dunkel" : "Hell");
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
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
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

    g.setColour (theme.panel);
    g.fillRoundedRectangle (12.0f, 248.0f, 656.0f, 156.0f, 8.0f);
    g.setColour (theme.line);
    g.drawRoundedRectangle (12.5f, 248.5f, 655.0f, 155.0f, 8.0f, 1.0f);
    g.drawVerticalLine (340, 266.0f, 380.0f);

}

void MovesEditor::resized()
{
    if (sizeReady)
    {
        processor.uiWidth.store (getWidth());
        processor.uiHeight.store (getHeight());
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

    events.setBounds (12, 84, w - 24, 154);
    auto placeKnob = [] (Knob& k, int x)
    {
        k.label.setBounds (x, 256, 220, 18);
        // Same 110 x 112 slider bounds and 80 x 18 value box as Radiophone's
        // rotary control: preserve the shared JUCE V4 arc/thumb proportions.
        k.slider.setBounds (x + 55, 274, 110, 112);
    };
    placeKnob (movement, 66);
    placeKnob (level, 394);
    libraryStatus.setBounds (24, 388, w - 48, 14);
    helpScroll.toFront (false);
}

void EventDisplay::refresh()
{
    const auto counter=processor.getDisplayCounter();
    const bool changed=counter!=lastCounter;
    idle=changed ? 0 : idle+1;
    meter=juce::jmax(meter*.86f,changed ? processor.getOutputPeak() : 0.f);
    mask=idle<4 ? processor.getActivityMask() : 0;
    lastCounter=counter;
    const auto seed=processor.getSequenceSeed();
    if(seed!=lastSeed) {preview.setSeed(seed);lastSeed=seed;}
    repaint();
}

void EventDisplay::paint(juce::Graphics& g)
{
    auto bounds=getLocalBounds().toFloat();
    g.setColour(bg);g.fillRoundedRectangle(bounds,8);
    g.setColour(line);g.drawRoundedRectangle(bounds.reduced(.5f),8,1);
    g.setFont(juce::FontOptions(12,juce::Font::bold));g.setColour(accent);
    g.drawText(u8("EREIGNISSE"),16,10,180,20,juce::Justification::centredLeft);
    const juce::String status=mask==3 ? u8("Bewegung + Atemgeräusch") : mask==1 ? "Bewegung" : mask==2 ? u8("Atemgeräusch") : "Ruhephase";
    g.setColour(text);g.setFont(juce::FontOptions(12));
    g.drawText(status,getWidth()-300,10,282,20,juce::Justification::centredRight);
    const float x0=108, x1=float(getWidth()-20), y0=52, y1=82;
    const double now=processor.getDisplayPosition(), begin=now-3, span=8;
    auto x=[&](double t){return x0+float((t-begin)/span)*(x1-x0);};
    g.setColour(dim);g.setFont(juce::FontOptions(11));
    g.drawText("Bewegung",16,int(y0)-8,86,18,juce::Justification::centredLeft);
    g.drawText(u8("Atem/Nase"),16,int(y1)-8,86,18,juce::Justification::centredLeft);
    for(int i=-3;i<=5;++i)
    {
        const float px=x(now+i);g.setColour(line);g.drawVerticalLine(int(px),38,100);
        if(i!=0) {g.setColour(dim);g.drawText(juce::String(i)+" s",int(px)-18,101,36,15,juce::Justification::centred);}
    }
    const float activity=processor.apvts.getRawParameterValue("movement")->load();
    const int64_t first=std::max<int64_t>(0,int64_t(std::floor((begin-4)/mv::Engine::kCellSeconds)));
    const int64_t last=int64_t(std::ceil((begin+span)/mv::Engine::kCellSeconds));
    g.saveState();g.reduceClipRegion(int(x0),38,int(x1-x0),62);
    for(int64_t cell=first;cell<=last;++cell)
    {
        mv::Engine::Event ev;if(!preview.eventAt(cell,activity,ev))continue;
        const double start=double(ev.start)/48000;
        if(start+ev.duration<begin)continue;
        const bool movement=ev.category==mv::Category::movement;
        const float px=x(start),width=juce::jmax(2.f,x(start+ev.duration)-px);
        const float cy=movement?y0:y1;
        const auto colour=movement?accent:text.interpolatedWith(bg,.4f);
        // Rounded event bars communicate duration without invented audio waveforms.
        g.setColour(colour.withAlpha(start<=now&&start+ev.duration>now?.95f:.42f));
        g.fillRoundedRectangle(px,cy-6,width,12,5);
    }
    g.restoreState();g.setColour(text.withAlpha(.9f));g.drawVerticalLine(int(x(now)),38,100);
    g.setFont(juce::FontOptions(10));g.drawText("JETZT",int(x(now))-25,101,50,15,juce::Justification::centred);
    const float db=meter>1e-5f?juce::Decibels::gainToDecibels(meter):-100;
    g.setColour(dim);g.drawText("AUSGANG",16,127,82,16,juce::Justification::centredLeft);
    auto bar=juce::Rectangle<float>(x0,133,x1-x0-82,6);
    g.setColour(line);g.fillRoundedRectangle(bar,3);
    g.setColour(accent);g.fillRoundedRectangle(bar.withWidth(bar.getWidth()*juce::jlimit(0.f,1.f,(db+60)/60)),3);
    g.setColour(text);g.drawText(db<=-60?u8("< −60 dBFS"):juce::String(db,1).replaceCharacter('.',',')+" dBFS",getWidth()-108,123,88,20,juce::Justification::centredRight);
}
