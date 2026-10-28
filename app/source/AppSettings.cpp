#include "AppSettings.h"

namespace {

juce::PropertiesFile::Options options() {
    juce::PropertiesFile::Options o;
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 0;  // save on every change
    return o;
}

}  // namespace

AppSettings::AppSettings() : props_(folder().getChildFile("settings.xml"), options()) {}

juce::File AppSettings::folder() {
    const auto env = juce::SystemStats::getEnvironmentVariable("KS32_SETTINGS_DIR", {});
    return env.isNotEmpty() ? juce::File(env)
                            : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("KS-32");
}

juce::File AppSettings::romFolder() const {
    const auto path = props_.getValue("romFolder");
    return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
}

void AppSettings::setRomFolder(const juce::File& dir) { props_.setValue("romFolder", dir.getFullPathName()); }

juce::File AppSettings::memoryFile() const {
    const auto path = props_.getValue("memoryFile");
    return juce::File::isAbsolutePath(path) ? juce::File(path) : folder().getChildFile("ks32-memory.bin");
}

void AppSettings::setMemoryFile(const juce::File& file) { props_.setValue("memoryFile", file.getFullPathName()); }
