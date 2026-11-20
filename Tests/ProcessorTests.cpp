#include "PluginProcessor.h"
#include "TestSupport.h"
#include <thread>

static void parameter (MovesProcessor& p, const char* id, float value)
{
    auto* v = p.apvts.getParameter (id); CHECK (v != nullptr);
    v->setValueNotifyingHost (v->convertTo0to1 (value));
}
static void fill (juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (c, i, 0.1f * std::sin (0.07f * (float) i));
}
static void layout (MovesProcessor& p, int out)
{
    auto buses = p.getBusesLayout();
    buses.outputBuses.set (0, out == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    CHECK (p.setBusesLayout (buses));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::MidiBuffer midi;

    // Verschiedene Abtastraten, Blockgroessen und Bus-Layouts: kein Absturz, keine
    // Audio-Thread-Allokation, nur endliche Werte.
    for (double fs : { 22050., 44100., 48000., 96000., 192000. })
    for (int out : { 1, 2 })
    {
        MovesProcessor p; layout (p, out); parameter (p, "movement", 0.7f); p.prepareToPlay (fs, 512);
        for (int n : { 0, 1, 3, 32, 4096, 4097, 65537, 7 })
        {
            juce::AudioBuffer<float> b (out, n); fill (b);
            { NoAllocations guard; p.processBlock (b, midi); }
            for (int i = 0; i < n; ++i)
            {
                CHECK (std::isfinite (b.getSample (0, i)));
                if (out == 2) CHECK (b.getSample (0, i) == b.getSample (1, i));
            }
        }
    }
    std::cout << "OK: Abtastraten/Blockgroessen/Layouts ohne Fehler, ohne Allokation\n";

    // Bypass: MOVES hat keinen Eingang, der durchgereicht werden koennte - im Bypass bleibt es stumm.
    {
        MovesProcessor p; p.prepareToPlay (48000, 256);
        juce::AudioBuffer<float> b (2, 256); fill (b);
        p.processBlockBypassed (b, midi);
        for (int i = 0; i < 256; ++i) CHECK (b.getSample (0, i) == 0.0f);
        std::cout << "OK: Bypass liefert Stille\n";
    }

    // Blockaufteilung: dasselbe Zeitfenster am Stueck oder in viele kleine Haeppchen verarbeitet
    // muss dasselbe Ergebnis liefern (faengt Offset-/Kopierfehler beim Chunking ab).
    {
        MovesProcessor a, b;
        a.prepareToPlay (48000, 16); b.prepareToPlay (48000, 16);
        parameter (a, "movement", 0.8f); parameter (b, "movement", 0.8f);
        const int total = 65537;
        juce::AudioBuffer<float> whole (2, total), split (2, total);
        whole.clear(); split.clear();
        a.processBlock (whole, midi);
        for (int i = 0; i < total; i += 31)
        {
            juce::AudioBuffer<float> view (split.getArrayOfWritePointers(), 2, i, std::min (31, total - i));
            b.processBlock (view, midi);
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
                CHECK (whole.getSample (c, i) == split.getSample (c, i));
        std::cout << "OK: Blockaufteilung aendert das Ergebnis nicht\n";
    }

    // Zustand speichern/wiederherstellen (Parameter + Theme/Fenstergroesse); defekte Daten
    // duerfen den vorherigen Zustand nicht veraendern.
    {
        MovesProcessor a, b;
        a.prepareToPlay (48000, 256); b.prepareToPlay (48000, 256);
        parameter (a, "movement", 0.9f); parameter (a, "level", -9.0f);
        a.uiTheme = 1; a.uiWidth = 900; a.uiHeight = 556;
        juce::MemoryBlock state; a.getStateInformation (state);
        b.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (juce::approximatelyEqual (b.apvts.getRawParameterValue ("movement")->load(), 0.9f));
        CHECK (juce::approximatelyEqual (b.apvts.getRawParameterValue ("level")->load(), -9.0f));
        CHECK (b.uiTheme == 1 && b.uiWidth == 900 && b.uiHeight == 556);
        b.setStateInformation ("broken", 6);
        CHECK (juce::approximatelyEqual (b.apvts.getRawParameterValue ("movement")->load(), 0.9f));
        std::cout << "OK: Zustand wird gespeichert/wiederhergestellt, defekte Daten werden ignoriert\n";
    }

    // GUI-Lebenszyklus: mehrfaches Erzeugen/Schliessen ohne Fehler, kein Display-Server noetig.
    {
        MovesProcessor p; p.prepareToPlay (48000, 256);
        for (int n = 0; n < 10; ++n)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            editor->setSize (680, 420);
            CHECK (p.uiWidth == 680);
        }
        std::cout << "OK: GUI laesst sich wiederholt oeffnen/schliessen\n";
    }

    // Mehrere gleichzeitig laufende Instanzen plus Automation/State-Restore waehrend der Wiedergabe.
    {
        MovesProcessor a, b;
        a.prepareToPlay (48000, 256); b.prepareToPlay (48000, 256);
        parameter (a, "movement", 0.5f);
        juce::MemoryBlock state; a.getStateInformation (state);
        std::atomic<bool> done { false };
        std::thread audio ([&] {
            juce::AudioBuffer<float> block (2, 127); juce::MidiBuffer m;
            for (int n = 0; n < 1000; ++n) { fill (block); NoAllocations guard; a.processBlock (block, m); b.processBlock (block, m); }
            done = true;
        });
        while (! done.load()) { b.setStateInformation (state.getData(), (int) state.getSize()); parameter (a, "movement", 0.3f); }
        audio.join();
        std::cout << "OK: mehrere Instanzen gleichzeitig, unabhaengig voneinander\n";
    }

    std::cout << "Alle MOVES-Processor-Tests erfolgreich.\n";
    return 0;
}
