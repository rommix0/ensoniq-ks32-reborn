#include "Editor.h"

#include "Processor.h"

Ks32Editor::Ks32Editor(Ks32Processor& p) : AudioProcessorEditor(p), proc_(p) { setSize(680, 240); }

void Ks32Editor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(20.0f));
    g.drawFittedText(proc_.engine() ? juce::String("KS-32") : proc_.romError(), getLocalBounds().reduced(16),
                     juce::Justification::centred, 4);
}
