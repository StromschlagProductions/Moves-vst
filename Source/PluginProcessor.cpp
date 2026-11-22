#include "PluginProcessor.h"
#include "PluginEditor.h"
#if MOVES_HAS_SAMPLES
 #include <BinaryData.h>
 #include <algorithm>
#endif

namespace
{
constexpr const char* kMovementPrefix = "movement_";
constexpr const char* kBreathPrefix = "breath_";
}

juce::AudioProcessorValueTreeState::ParameterLayout MovesProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; }).withValueFromStringFunction ([] (const String& text) { return text.replaceCharacter (',', '.').getFloatValue() * 0.01f; });
    auto decibel = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1).replaceCharacter ('.', ',') + " dB"; }).withValueFromStringFunction ([] (const String& text) { return text.replaceCharacter (',', '.').getFloatValue(); });

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "movement", 1 }, juce::String (juce::CharPointer_UTF8 ("Aktivit\xc3\xa4t")),
                                                       NormalisableRange<float> (0.0f, 1.0f), 0.4f, percent));

    NormalisableRange<float> levelRange (-60.0f, 12.0f, 0.1f);
    levelRange.setSkewForCentre (-18.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "level", 1 }, juce::String (juce::CharPointer_UTF8 ("Lautst\xc3\xa4rke")),
                                                       levelRange, -6.0f, decibel));
    return layout;
}

MovesProcessor::MovesProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MOVES", createLayout())
{
    pMovement = apvts.getRawParameterValue ("movement");
    pLevel = apvts.getRawParameterValue ("level");
    uint32_t initialSeed = 0;
    while (initialSeed == 0) initialSeed = (uint32_t) juce::Random::getSystemRandom().nextInt();
    sequenceSeed.store (initialSeed);
    loadSampleLibrary();
}

#if MOVES_HAS_SAMPLES
static bool decodeWavFromMemory (const void* data, int size, mv::SampleClip& out)
{
    juce::WavAudioFormat wav;
    auto stream = std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false);
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;
    out.sourceRate = reader->sampleRate;
    juce::AudioBuffer<float> buf (1, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, false);
    out.data.assign (buf.getReadPointer (0), buf.getReadPointer (0) + buf.getNumSamples());
    return true;
}
#endif

// Liest alle eingebetteten movement_*.wav / breath_*.wav Ressourcen (beliebige Anzahl,
// siehe CMakeLists: der komplette Assets/samples-Ordner wird eingebettet). Eine einzelne
// defekte oder nicht lesbare Datei wird uebersprungen, nicht die ganze Bibliothek verworfen.
void MovesProcessor::loadSampleLibrary()
{
    auto newBank = std::make_shared<mv::SampleBank>();
#if MOVES_HAS_SAMPLES
    struct Entry { juce::String filename; const char* data; int size; };
    std::vector<Entry> entries;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const char* resourceName = BinaryData::namedResourceList[i];
        const char* original = BinaryData::getNamedResourceOriginalFilename (resourceName);
        if (original == nullptr) continue;
        int size = 0;
        const char* data = BinaryData::getNamedResource (resourceName, size);
        if (data == nullptr || size <= 0) continue;
        entries.push_back ({ juce::String (original), data, size });
    }
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.filename < b.filename; });

    for (const auto& e : entries)
    {
        const bool isMovement = e.filename.startsWithIgnoreCase (kMovementPrefix) && e.filename.endsWithIgnoreCase (".wav");
        const bool isBreath = e.filename.startsWithIgnoreCase (kBreathPrefix) && e.filename.endsWithIgnoreCase (".wav");
        if (! isMovement && ! isBreath) continue;

        mv::SampleClip clip;
        if (! decodeWavFromMemory (e.data, e.size, clip)) continue;   // defekte Datei: ueberspringen, Rest bleibt nutzbar
        if (clip.data.empty()) continue;

        mv::conditionClip (clip, isMovement ? mv::Category::movement : mv::Category::breath);
        if (isMovement) newBank->movements.push_back (std::move (clip));
        else newBank->breaths.push_back (std::move (clip));
    }
#endif
    bank = newBank;
    engine.setBank (bank);
}

void MovesProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    appliedSeed = sequenceSeed.load();
    engine.setSeed (appliedSeed);
    engine.setBank (bank);
    const size_t bufSize = (size_t) juce::jmax (samplesPerBlock, 4096);
    leftBuf.assign (bufSize, 0.0f);
    rightBuf.assign (bufSize, 0.0f);
    freeRunningPosition = 0;
    bypassMix = -1;
}

void MovesProcessor::reset()
{
    engine.reset();
    freeRunningPosition = 0;
    bypassMix = -1;
}

bool MovesProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;   // MOVES erzeugt den Klang selbst, es wird kein Eingang verarbeitet
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void MovesProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, false);
}

void MovesProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    process (buffer, true);
}

void MovesProcessor::process (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (numSamples == 0 || numCh == 0)
        return;

    if (leftBuf.empty()) { buffer.clear(); return; }   // defensiv: Host rief vor prepareToPlay auf

    int64_t blockStartSample = freeRunningPosition;
    if (auto* ph = getPlayHead())
    {
        if (auto position = ph->getPosition())
            if (auto samples = position->getTimeInSamples())
                blockStartSample = *samples;
    }

    const auto wantedSeed = sequenceSeed.load();
    if (wantedSeed != appliedSeed) { engine.setSeed (wantedSeed); appliedSeed = wantedSeed; }
    mv::Params p;
    p.movement = pMovement->load();
    p.levelDb = pLevel->load();

    float* left = leftBuf.data();
    float* right = rightBuf.data();
    const float bypassTarget = bypassed ? 0.f : 1.f;
    if (bypassMix < 0) bypassMix = bypassTarget;
    const float bypassStep = 1.f / float (.040 * engine.getSampleRate());
    float peak = 0;
    int mask = 0;
    for (int offset = 0; offset < numSamples;)
    {
        const int count = juce::jmin ((int) leftBuf.size(), numSamples - offset);
        engine.process (left, right, count, p, blockStartSample + offset);
        mask |= engine.getActiveMask();
        for (int i = 0; i < count; ++i)
        {
            bypassMix += juce::jlimit (-bypassStep, bypassStep, bypassTarget - bypassMix);
            const float l = std::isfinite (left[i]) ? left[i] * bypassMix : 0.0f;
            const float r = std::isfinite (right[i]) ? right[i] * bypassMix : 0.0f;
            peak = juce::jmax (peak, std::abs (l), std::abs (r));
            if (numCh == 1)
                buffer.setSample (0, offset + i, 0.5f * (l + r));
            else
                for (int ch = 0; ch < numCh; ++ch)
                    buffer.setSample (ch, offset + i, ch == 0 ? l : r);
        }
        offset += count;
    }

    freeRunningPosition = blockStartSample + numSamples;
    displayPosition.store ((double) freeRunningPosition / engine.getSampleRate());
    outputPeak.store (peak);
    activityMask.store (bypassMix > .001f ? mask : 0);
    displayCounter.fetch_add (1);
}

void MovesProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("sequenceSeed", (juce::int64) sequenceSeed.load(), nullptr);
    state.setProperty ("theme", uiTheme.load(), nullptr);
    state.setProperty ("uiW", uiWidth.load(), nullptr);
    state.setProperty ("uiH", uiHeight.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MovesProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName ("MOVES"))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            // Legacy sessions retain the old fixed sequence; new instances have their own seed.
            const auto savedSeed = (uint32_t) (juce::int64) state.getProperty ("sequenceSeed", (juce::int64) 0x4D6F7665u);
            sequenceSeed.store (savedSeed);
            uiTheme.store ((int) state.getProperty ("theme", 0));
            uiWidth.store (juce::jlimit (340, 2040, (int) state.getProperty ("uiW", 680)));
            uiHeight.store (juce::jlimit (210, 1260, (int) state.getProperty ("uiH", 420)));
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* MovesProcessor::createEditor() { return new MovesEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MovesProcessor(); }
