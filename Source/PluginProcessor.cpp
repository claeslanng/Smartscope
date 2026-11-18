#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstring>

//==============================================================================
SmartscopeAudioProcessor::SmartscopeAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Smartscope", createLayout())
{
    // Allocated once, up front, so the editor can read it at any time without
    // the risk of it being reallocated underneath it.
    display = std::make_unique<float[]> ((size_t) maxDisplaySamples);
    std::memset (display.get(), 0, sizeof (float) * (size_t) maxDisplaySamples);

    lengthParam = apvts.getRawParameterValue (ParamIDs::length);
}

juce::AudioProcessorValueTreeState::ParameterLayout SmartscopeAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray names;
    for (auto& c : lengthChoices)
        names.add (c.name);

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::length, 1 }, "Length",
                                                        names, defaultLengthIndex));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::gain, 1 }, "Display Gain",
        NormalisableRange<float> (-12.0f, 36.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::lowpassOn, 1 }, "Low-pass", false));

    NormalisableRange<float> hzRange (20.0f, 800.0f, 1.0f);
    hzRange.setSkewForCentre (120.0f);
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::lowpassHz, 1 }, "Low-pass Freq", hzRange, 150.0f,
        AudioParameterFloatAttributes().withLabel ("Hz")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

    return layout;
}

bool SmartscopeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void SmartscopeAudioProcessor::prepareToPlay (double sampleRate, int)
{
    if (sampleRate > 0.0)
        currentSampleRate.store (sampleRate);

    lastBlockSynced = false;
}

void SmartscopeAudioProcessor::resetWindow (int newWindowSamples)
{
    // Clear everything that might be shown, including any leftovers from a longer window.
    const int toClear = juce::jmax (newWindowSamples, clearedUpTo);
    std::memset (display.get(), 0, sizeof (float) * (size_t) toClear);
    clearedUpTo = newWindowSamples;

    freePos = 0;
    lastBlockSynced = false;
    writePos.store (0, std::memory_order_release);
    windowSamples.store (newWindowSamples, std::memory_order_release);
    resetCounter.fetch_add (1, std::memory_order_acq_rel);
}

void SmartscopeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    if (numSamples <= 0 || numIn <= 0)
        return;

    // The plugin never touches the audio: it is a pure pass-through.
    const float* left  = buffer.getReadPointer (0);
    const float* right = numIn > 1 ? buffer.getReadPointer (1) : left;

    //--------------------------------------------------------------------------
    // Musical position from the host
    double hostBpm = 120.0;
    bool playing = false;
    bool havePpq = false;
    double ppq = 0.0, barStart = 0.0;
    bool haveBarStart = false;
    int tsNum = 4, tsDen = 4;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0)
                hostBpm = *b;

            playing = pos->getIsPlaying();

            if (auto p = pos->getPpqPosition())
            {
                ppq = *p;
                havePpq = true;
            }

            if (auto bs = pos->getPpqPositionOfLastBarStart())
            {
                barStart = *bs;
                haveBarStart = true;
            }

            if (auto ts = pos->getTimeSignature(); ts.hasValue() && ts->numerator > 0 && ts->denominator > 0)
            {
                tsNum = ts->numerator;
                tsDen = ts->denominator;
            }
        }
    }

    bpm.store (hostBpm);
    hostPlaying.store (playing);

    const double sr = currentSampleRate.load();
    const double barQN = (double) tsNum * 4.0 / (double) tsDen;
    barQNAtomic.store (barQN);

    const int lengthIndex = juce::jlimit (0, numLengthChoices - 1, (int) std::lround (lengthParam->load()));
    const auto& choice = lengthChoices[lengthIndex];
    const double wantedQN = choice.bars > 0 ? barQN * (double) choice.bars : choice.wholeNotes * 4.0;

    const double samplesPerQN = sr * 60.0 / hostBpm;

    // Very slow tempos at long lengths could exceed the buffer: shorten the window in that case.
    double qn = wantedQN;
    bool isClamped = false;
    if (qn * samplesPerQN > (double) maxDisplaySamples)
    {
        qn = (double) maxDisplaySamples / samplesPerQN;
        isClamped = true;
    }
    clamped.store (isClamped);

    const int newWindow = juce::jlimit (16, maxDisplaySamples, (int) std::lround (qn * samplesPerQN));

    if (newWindow != windowSamples.load (std::memory_order_relaxed) || std::abs (qn - windowQN.load()) > 1.0e-9)
    {
        windowQN.store (qn);
        resetWindow (newWindow);
    }

    const int N = newWindow;

    //--------------------------------------------------------------------------
    // Decide where this block lands inside the window
    int pos = freePos;

    if (playing && havePpq)
    {
        // Align short windows to the bar line so that e.g. a 1/2 window always starts
        // on beat 1 or 3, even after tempo-map or time-signature quirks.
        double rel = ppq;
        if (haveBarStart && qn <= barQN + 1.0e-9)
            rel = ppq - barStart;

        double phaseQN = std::fmod (rel, qn);
        if (phaseQN < 0.0)
            phaseQN += qn;

        int hostPos = (int) std::floor (phaseQN * samplesPerQN + 1.0e-6);
        hostPos = juce::jlimit (0, N - 1, hostPos);

        // Hosts report positions with rounding noise. If we are within a couple of samples
        // of where the previous block ended, keep the continuous position to avoid
        // doubled or skipped samples.
        if (lastBlockSynced)
        {
            int diff = std::abs (hostPos - freePos);
            diff = juce::jmin (diff, N - diff);
            if (diff <= 2)
                hostPos = freePos;
        }

        pos = hostPos;
        lastBlockSynced = true;
        synced.store (true);
    }
    else
    {
        // Transport stopped (or no host position): free-run at the host tempo while
        // there is signal, and keep the last picture when it goes silent so it can
        // still be zoomed and filtered.
        lastBlockSynced = false;
        synced.store (false);

        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (left[i]), std::abs (right[i]));

        if (peak < 1.0e-5f)
            return;
    }

    //--------------------------------------------------------------------------
    float* d = display.get();
    for (int i = 0; i < numSamples; ++i)
    {
        d[pos] = 0.5f * (left[i] + right[i]);
        if (++pos >= N)
            pos = 0;
    }

    freePos = pos;
    writePos.store (pos, std::memory_order_release);
    writeCounter.fetch_add (1, std::memory_order_acq_rel);
}

//==============================================================================
juce::AudioProcessorEditor* SmartscopeAudioProcessor::createEditor()
{
    return new SmartscopeAudioProcessorEditor (*this);
}

void SmartscopeAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("editorWidth", editorWidth, nullptr);
    state.setProperty ("editorHeight", editorHeight, nullptr);
    state.setProperty ("theme", themeIndex.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SmartscopeAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            editorWidth  = (int) state.getProperty ("editorWidth", editorWidth);
            editorHeight = (int) state.getProperty ("editorHeight", editorHeight);
            themeIndex.store ((int) state.getProperty ("theme", 0));
            apvts.replaceState (state);
        }
    }
}

//==============================================================================
#ifndef SMARTSCOPE_TEST_BUILD
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SmartscopeAudioProcessor();
}
#endif
