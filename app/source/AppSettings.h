#pragma once
// The app's settings: %APPDATA%\KS-32\settings.xml, shared by the Standalone and VST3 builds
// (design section 6.4). Each change is saved at once.
#include <juce_data_structures/juce_data_structures.h>

class AppSettings {
public:
    AppSettings();
    // %APPDATA%\KS-32, or the folder in the environment variable KS32_SETTINGS_DIR when it is set
    // (checks that must not touch the user's own settings and memory).
    static juce::File folder();

    juce::File romFolder() const;  // juce::File() until one is chosen
    void setRomFolder(const juce::File& dir);
    juce::File memoryFile() const;  // default: ks32-memory.bin in folder()
    void setMemoryFile(const juce::File& file);

private:
    juce::PropertiesFile props_;
};
