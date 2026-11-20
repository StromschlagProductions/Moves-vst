#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "MovesDSP.h"

class MovesProcessor : public juce::AudioProcessor
{
public:
    MovesProcessor();
    ~MovesProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Moves"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // fuer die Anzeige in der Oberflaeche (wie viele Samples geladen werden konnten)
    int numMovementSamples() const { return bank ? (int) bank->movements.size() : 0; }
    int numBreathSamples() const { return bank ? (int) bank->breaths.size() : 0; }

    std::atomic<int> uiTheme { 0 }, uiWidth { 680 }, uiHeight { 420 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void loadSampleLibrary();
    void process (juce::AudioBuffer<float>&, bool bypassed);

    mv::Engine engine;
    std::shared_ptr<mv::SampleBank> bank;
    std::vector<float> leftBuf, rightBuf;

    // Fallback-Zaehler, falls der Host keine gueltige Projektposition liefert (z. B. manche
    // Standalone-Testlaeufe): dann wird intern ab 0 fortlaufend weitergezaehlt. Siehe
    // Known-Limitations-Hinweis in der Dokumentation.
    int64_t freeRunningPosition = 0;

    std::atomic<float>* pMovement = nullptr;
    std::atomic<float>* pLevel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MovesProcessor)
};
