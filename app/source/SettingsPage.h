#pragma once
// The settings page, as the "Settings" artboard draws it with design section 6.4's changes: the
// ROM folder and its six files, the Standalone wrapper's audio and MIDI devices (hidden in the
// VST3), and the memory file with Erase (and, in a plugin, Copy to shared memory, A22).
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>
#include <vector>

#include "RomFolder.h"

class Ks32Processor;

class SettingsPage : public juce::Component, private juce::Timer {
public:
    explicit SettingsPage(Ks32Processor& p);
    ~SettingsPage() override;

    std::function<void()> onChange;  // after the machine was re-created or erased

    void paint(juce::Graphics& g) override;
    void resized() override;
    void childBoundsChanged(juce::Component* child) override;

private:
    void timerCallback() override;  // the underrun count, and a memory error raised since (a failed save)
    int layout();    // places the children and the painted rows; returns the page height
    void refresh();  // re-reads the settings, the ROM folder and the memory note
    void changed();
    void chooseRomFolder();
    void chooseMemoryFile();
    void confirmErase();
    void confirmCopy();  // Project mode: this instance's memory to the shared file (A22)

    Ks32Processor& proc_;
    juce::LookAndFeel_V4 lnf_;
    juce::Label romField_, memField_;
    juce::TextButton chooseRoms_, changeMem_, erase_, copy_;  // copy_: Project mode only
    std::unique_ptr<juce::AudioDeviceSelectorComponent> devices_;  // Standalone only
    std::unique_ptr<juce::FileChooser> chooser_;
    std::vector<RomFileStatus> roms_;
    juce::String memNote_;
    juce::Rectangle<int> header_, romsHead_, romList_, devHead_, rateLine_, memHead_, memLabel_, memNoteArea_;
    bool laying_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};
