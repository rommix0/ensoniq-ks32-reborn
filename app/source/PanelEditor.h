#pragma once
// The front-panel editor (design section 6.3): the "Front panel" artboard drawn from the JUCE-free
// layout table (src/app/panel_layout.h), scaled uniformly to the window (A13). The display and LEDs
// follow the engine's snapshot (A7, A8); the buttons post press and release, with the chord latch
// (A9); the sliders and wheels are the host parameters (A17), and the keys play with a strike (A10). The
// gear opens the settings page.
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "app/engine.h"

class Ks32Processor;

class PanelEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PanelEditor(Ks32Processor& p);
    ~PanelEditor() override;
    void resized() override;

    // Used by the parts.
    // False when the queue is full (A4), there is no engine, or the engine was just replaced (the
    // parts are then reset and nothing reaches the new engine). The parts retry from the timer.
    bool post(const ens::app::Event& e);
    juce::RangedAudioParameter* param(const char* id);  // a host parameter (A17)
    void openSettings();
    const ens::app::Snapshot& snap() const { return snap_; }
    bool blinkOn() const { return blinkOn_; }
    bool hexCodes() const { return hexCodes_; }
    void toggleHexCodes();
    juce::String romError() const;  // empty while the engine runs
    juce::String midiInput() const { return midiInput_; }

private:
    void timerCallback() override;
    juce::String findMidiInput() const;
    bool sameEngine();  // false, after resetting the parts, when the engine was replaced

    Ks32Processor& proc_;
    ens::app::Engine* engine_ = nullptr;  // the engine the parts' state belongs to
    std::unique_ptr<juce::Component> board_;  // 2040 x 720 design units, scaled by a transform
    juce::TooltipWindow tooltips_{this};
    std::unique_ptr<juce::DocumentWindow> settings_;
    ens::app::Snapshot snap_{};
    bool blinkOn_ = true, hexCodes_ = false, foreground_ = true;
    juce::String midiInput_;
    int ticks_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PanelEditor)
};
