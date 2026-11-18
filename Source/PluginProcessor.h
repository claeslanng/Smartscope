#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>

namespace ParamIDs
{
    inline constexpr const char* length     = "length";
    inline constexpr const char* gain       = "gain";
    inline constexpr const char* lowpassOn  = "lowpassOn";
    inline constexpr const char* lowpassHz  = "lowpassHz";
}

//==============================================================================
/** Window length choices. Fractions are of a whole note (1/4 = one quarter note),
    bar lengths follow the host's time signature. */
struct LengthChoice
{
    const char* name;
    double wholeNotes; // used when bars == 0
    int bars;
};

inline constexpr LengthChoice lengthChoices[] = {
    { "1/16",   1.0 / 16.0, 0 },
    { "1/8",    1.0 / 8.0,  0 },
    { "1/4",    1.0 / 4.0,  0 },
    { "1/2",    1.0 / 2.0,  0 },
    { "1 bar",  0.0,        1 },
    { "2 bars", 0.0,        2 },
    { "4 bars", 0.0,        4 },
};
inline constexpr int numLengthChoices = (int) (sizeof (lengthChoices) / sizeof (lengthChoices[0]));
inline constexpr int defaultLengthIndex = 3;

/** Layer 0 is the main input, layers 1..3 are the optional sidechain inputs. */
inline constexpr int numLayers = 4;
inline constexpr int numSidechains = numLayers - 1;

//==============================================================================
class SmartscopeAudioProcessor : public juce::AudioProcessor
{
public:
    SmartscopeAudioProcessor();
    ~SmartscopeAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Smartscope"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    //==============================================================================
    // Display data shared with the editor. The audio thread writes samples into
    // `display` at the position that corresponds to the host's musical position
    // inside the current window, so index 0 is always the start of the window.
    static constexpr int maxDisplaySamples = 1 << 21; // ~43 s at 48 kHz, ~10 s at 192 kHz

    const float* getDisplayData (int layer = 0) const noexcept { return display[juce::jlimit (0, numLayers - 1, layer)].get(); }

    /** True if the host has this sidechain bus enabled (layer 0 is always connected). */
    bool isLayerConnected (int layer) const noexcept { return layerConnected[juce::jlimit (0, numLayers - 1, layer)].load(); }

    /** UI-only settings (colours, opacities, toggles). Message thread only. Saved with the project. */
    juce::ValueTree getUISettings() { return apvts.state.getOrCreateChildWithName ("UI", nullptr); }
    int    getWindowSamples() const noexcept       { return windowSamples.load (std::memory_order_acquire); }
    int    getWritePosition() const noexcept       { return writePos.load (std::memory_order_acquire); }
    uint32_t getWriteCounter() const noexcept      { return writeCounter.load (std::memory_order_acquire); }
    uint32_t getResetCounter() const noexcept      { return resetCounter.load (std::memory_order_acquire); }
    double getBpm() const noexcept                 { return bpm.load(); }
    double getDisplaySampleRate() const noexcept   { return currentSampleRate.load(); }
    double getWindowQuarterNotes() const noexcept  { return windowQN.load(); }
    double getBarQuarterNotes() const noexcept     { return barQNAtomic.load(); }
    bool   isHostPlaying() const noexcept          { return hostPlaying.load(); }
    bool   isSynced() const noexcept               { return synced.load(); }
    bool   isClamped() const noexcept              { return clamped.load(); }

    // Editor size, saved with the project
    int editorWidth = 900, editorHeight = 480;

    // Colour theme, saved with the project
    std::atomic<int> themeIndex { 0 };

private:
    std::unique_ptr<float[]> display[numLayers];
    std::atomic<bool> layerConnected[numLayers] {};

    std::atomic<int>      windowSamples { 0 };
    std::atomic<int>      writePos { 0 };
    std::atomic<uint32_t> writeCounter { 0 };
    std::atomic<uint32_t> resetCounter { 0 };
    std::atomic<double>   bpm { 120.0 };
    std::atomic<double>   currentSampleRate { 44100.0 };
    std::atomic<double>   windowQN { 2.0 };
    std::atomic<double>   barQNAtomic { 4.0 };
    std::atomic<bool>     hostPlaying { false };
    std::atomic<bool>     synced { false };
    std::atomic<bool>     clamped { false };

    // audio-thread only
    int    clearedUpTo = 0;
    int    freePos = 0;
    bool   lastBlockSynced = false;

    std::atomic<float>* lengthParam = nullptr;

    void resetWindow (int newWindowSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartscopeAudioProcessor)
};
