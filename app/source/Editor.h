#pragma once
// Placeholder editor (Task 6 replaces it): the product name or the ROM error, and a temporary
// button that opens the settings page in its own window.
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

class Ks32Processor;

class Ks32Editor : public juce::AudioProcessorEditor {
public:
    explicit Ks32Editor(Ks32Processor& p);
    ~Ks32Editor() override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void openSettings();

    Ks32Processor& proc_;
    juce::TextButton settingsButton_{"Settings"};
    std::unique_ptr<juce::DocumentWindow> settings_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Ks32Editor)
};
