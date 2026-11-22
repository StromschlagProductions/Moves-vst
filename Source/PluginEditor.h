#pragma once
#include "PluginProcessor.h"

// Onboard-Hilfe: formatierter Text (Ueberschriften fett in Akzentfarbe), scrollbar in einem Viewport
class HelpView : public juce::Component
{
public:
    void setAttributed (const juce::AttributedString& s, juce::Colour background)
    {
        text = s; bg = background; relayout();
    }
    void setWidth (int w)
    {
        if (w != laidOutW) { laidOutW = w; relayout(); }
    }
    void paint (juce::Graphics& g) override
    {
        tl.draw (g, getLocalBounds().toFloat().reduced (14.0f, 10.0f));
    }
private:
    void relayout()
    {
        const int w = juce::jmax (120, laidOutW);
        tl.createLayout (text, (float) (w - 28));
        setSize (w, (int) std::ceil (tl.getHeight()) + 24);
        repaint();
    }
    juce::AttributedString text;
    juce::TextLayout tl;
    juce::Colour bg;
    int laidOutW = 0;
};

// Hilfe-Flaeche: Viewport mit abgerundetem Hintergrund (passt auf die Kartenflaeche darunter)
class HelpPanel : public juce::Viewport
{
public:
    juce::Colour fill, edge;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (fill);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    }
};

class EventDisplay : public juce::Component
{
public:
    explicit EventDisplay (MovesProcessor& p) : processor (p) { preview.prepare (48000); preview.setBank (p.getSampleBank()); lastSeed=p.getSequenceSeed(); preview.setSeed(lastSeed); }
    void colours (juce::Colour background, juce::Colour foreground, juce::Colour muted, juce::Colour highlight, juce::Colour border)
    { bg=background; text=foreground; dim=muted; accent=highlight; line=border; }
    void refresh();
    void paint (juce::Graphics&) override;
private:
    MovesProcessor& processor;
    mv::Engine preview;
    juce::Colour bg, text, dim, accent, line;
    uint64_t lastCounter=0;
    uint32_t lastSeed=0;
    float meter=0;
    int mask=0, idle=0;
};

class MovesEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MovesEditor (MovesProcessor&);
    ~MovesEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    struct Knob
    {
        juce::Slider slider; juce::Label label;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };
    struct Theme { juce::Colour bg, panel, accent, text, dim, line; bool dark; };

    void setupKnob (Knob&, const juce::String& paramId, const juce::String& text);
    void applyTheme();
    void updateLibraryStatus();
    void timerCallback() override;
    static Theme darkTheme();
    static Theme lightTheme();

    static constexpr int kBaseW = 680, kBaseH = 420;

    MovesProcessor& processor;
    juce::LookAndFeel_V4 look;
    juce::Component content;      // feste 680x420-Flaeche, wird skaliert
    float uiScale = 1.0f;
    Theme theme;
    bool sizeReady = false;   // erst nach dem Wiederherstellen der Groesse Fenstergroesse speichern
    juce::Image logo;

    Knob movement, level;
    EventDisplay events { processor };
    juce::Label libraryStatus;

    juce::TextButton themeButton;
    juce::TextButton helpButton;
    HelpPanel helpScroll;
    HelpView helpText;
    juce::TooltipWindow tooltipWindow { this, 400 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MovesEditor)
};
