#pragma once
// Placeholder editor (Task 6 replaces it): the product name, or the ROM error.
#include <juce_audio_processors/juce_audio_processors.h>

class Ks32Processor;

class Ks32Editor : public juce::AudioProcessorEditor {
public:
    explicit Ks32Editor(Ks32Processor& p);
    void paint(juce::Graphics& g) override;

private:
    Ks32Processor& proc_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ks32Editor)
};
