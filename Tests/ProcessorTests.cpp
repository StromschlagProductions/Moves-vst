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
        juce::MemoryBlock shared; a.getStateInformation (shared);
        b.setStateInformation (shared.getData(), (int) shared.getSize());
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

    // Real editor actions, not direct assignment of persistence fields.
    {
        MovesProcessor a;
        auto* activity=a.apvts.getParameter("movement");
        CHECK (std::abs(activity->convertFrom0to1(activity->getValueForText("40 %"))-.4f)<1e-5f);
        auto* level=a.apvts.getParameter("level");
        CHECK (std::abs(level->convertFrom0to1(level->getValueForText("-18,5 dB"))+18.5f)<.01f);
        std::unique_ptr<juce::AudioProcessorEditor> editor(a.createEditor());
        editor->setSize(900,556);
        std::function<void(juce::Component&)> clickTheme=[&](juce::Component& c) {
            if(auto* b=dynamic_cast<juce::TextButton*>(&c)) if(b->getButtonText()=="Dunkel") { b->onClick(); return; }
            for(auto* child:c.getChildren()) clickTheme(*child);
        };
        clickTheme(*editor);
        juce::MemoryBlock state;a.getStateInformation(state);
        MovesProcessor b;b.setStateInformation(state.getData(),int(state.getSize()));
        std::unique_ptr<juce::AudioProcessorEditor> restored(b.createEditor());
        CHECK (restored->getWidth()==900 && b.uiTheme.load()==1);
        CHECK (a.getSequenceSeed()==b.getSequenceSeed());
        std::cout << "OK: Texteingabe, echte GUI-Aenderung und gespeicherte Instanzfolge\n";
    }
    // Independent instances must actually sound different; a saved copy must match.
    {
        MovesProcessor a,b,c;
        CHECK (a.getSequenceSeed()!=b.getSequenceSeed());
        juce::MemoryBlock state;a.getStateInformation(state);
        c.setStateInformation(state.getData(),int(state.getSize()));
        for(auto* p:{&a,&b,&c}) {parameter(*p,"movement",1);p->prepareToPlay(48000,512);}
        bool different=false, audible=false;
        for(int n=0;n<1000;++n)
        {
            juce::AudioBuffer<float> x(2,512),y(2,512),z(2,512);
            a.processBlock(x,midi);b.processBlock(y,midi);c.processBlock(z,midi);
            for(int i=0;i<512;++i) {CHECK(x.getSample(0,i)==z.getSample(0,i));different|=x.getSample(0,i)!=y.getSample(0,i);audible|=x.getSample(0,i)!=0;}
        }
        CHECK(different && audible);
        std::cout << "OK: unabhaengige Instanzen, gespeicherte Kopie bitidentisch\n";
    }
    // Host-position integration with the real embedded library (including conditioning).
    {
        struct Head : juce::AudioPlayHead {
            int64_t sample=0;
            juce::Optional<PositionInfo> getPosition() const override {PositionInfo p;p.setTimeInSamples(sample);p.setIsPlaying(true);return p;}
        };
        MovesProcessor a,b; Head ha,hb;
        parameter(a,"movement",1);
        juce::MemoryBlock state;a.getStateInformation(state);b.setStateInformation(state.getData(),int(state.getSize()));
        a.setPlayHead(&ha);b.setPlayHead(&hb);
        a.prepareToPlay(48000,512);b.prepareToPlay(48000,512);
        std::vector<float> reference(48000*12);
        for(int offset=0;offset<int(reference.size());offset+=512)
        {
            const int n=std::min(512,int(reference.size())-offset);
            juce::AudioBuffer<float> audio(2,n);ha.sample=offset;a.processBlock(audio,midi);
            std::copy(audio.getReadPointer(0),audio.getReadPointer(0)+n,reference.begin()+offset);
        }
        for(int64_t start:{int64_t(341280),int64_t(123),int64_t(341280)})
        {
            for(int offset=0;offset<48000;offset+=193)
            {
                const int n=std::min(193,48000-offset);juce::AudioBuffer<float> audio(2,n);
                hb.sample=start+offset;b.processBlock(audio,midi);
                for(int i=0;i<n;++i)CHECK(audio.getSample(0,i)==reference[size_t(start+offset+i)]);
            }
        }
        std::cout << "OK: echte Hostposition und Samplebibliothek nach Spruengen bitidentisch\n";
    }
    std::cout << "Alle MOVES-Processor-Tests erfolgreich.\n";
    return 0;
}
