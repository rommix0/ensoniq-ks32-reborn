#include "SettingsPage.h"

#include "Processor.h"

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace {

// The artboard's colours.
const juce::Colour kPanel{0xff1f2023}, kField{0xff0d0d0e}, kFieldEdge{0xff3a3c41}, kText{0xffe6e7ea},
    kBody{0xffc9cbcf}, kDim{0xffa7aab0}, kStrip{0xff5fb3a1}, kButton{0xff3b3d42}, kButtonEdge{0xff55585e},
    kList{0xff18191b}, kListEdge{0xff2a2b2e}, kGood{0xff7fd3a0}, kBad{0xffff8a7f}, kBadEdge{0xff7a3631};

constexpr int kWidth = 760, kPadX = 32, kPadY = 28, kGap = 22, kHead = 17, kRow = 32;

juce::String dots(const char* s) { return juce::String(s) + juce::String::charToString(0x2026); }

// 48000 -> "48 000", as the artboard writes rates.
juce::String hz(double rate) {
    const auto r = juce::roundToInt(rate);
    return r < 1000 ? juce::String(r) : juce::String(r / 1000) + " " + juce::String(r % 1000).paddedLeft('0', 3);
}

juce::FontOptions font(float size, bool bold = false) {
    return juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain);
}

juce::FontOptions mono(float size) {
    return juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain);
}

void styleField(juce::Label& l) {
    l.setFont(mono(13.0f));
    l.setColour(juce::Label::backgroundColourId, kField);
    l.setColour(juce::Label::outlineColourId, kFieldEdge);
    l.setColour(juce::Label::textColourId, kText);
    l.setMinimumHorizontalScale(1.0f);
}

void styleButton(juce::TextButton& b, juce::Colour fill, juce::Colour text, juce::Colour edge) {
    b.setColour(juce::TextButton::buttonColourId, fill);
    b.setColour(juce::TextButton::textColourOffId, text);
    b.setColour(juce::ComboBox::outlineColourId, edge);  // LookAndFeel_V4 outlines buttons with it
}

void heading(juce::Graphics& g, const char* text, juce::Rectangle<int> r) {
    g.setColour(kStrip);
    g.setFont(font(13.0f, true).withKerningFactor(0.08f));
    g.drawText(text, r, juce::Justification::centredLeft);
}

}  // namespace

SettingsPage::SettingsPage(Ks32Processor& p)
    : proc_(p),
      lnf_(juce::LookAndFeel_V4::ColourScheme(kPanel, kField, kList, kFieldEdge, kText, kButton, kField, kStrip,
                                              kText)),
      chooseRoms_(dots("Choose folder")),
      changeMem_(dots("Change")),
      erase_(dots("Erase memory")) {
    setLookAndFeel(&lnf_);
    styleField(romField_);
    styleField(memField_);
    styleButton(chooseRoms_, kButton, kText, kButtonEdge);
    styleButton(changeMem_, kButton, kText, kButtonEdge);
    styleButton(erase_, juce::Colours::transparentBlack, kBad, kBadEdge);
    romField_.setTitle("ROM folder");
    memField_.setTitle("Memory file");
    for (auto* c : std::initializer_list<juce::Component*>{&romField_, &chooseRoms_, &memField_, &changeMem_, &erase_})
        addAndMakeVisible(c);
    chooseRoms_.onClick = [this] { chooseRomFolder(); };
    changeMem_.onClick = [this] { chooseMemoryFile(); };
    erase_.onClick = [this] { confirmErase(); };

#if JucePlugin_Build_Standalone
    // The Standalone wrapper's devices: audio out and MIDI inputs, no MIDI out (design section 6.4).
    // In the VST3 the host owns the devices and the section is not shown.
    if (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance()) {
            devices_ = std::make_unique<juce::AudioDeviceSelectorComponent>(holder->deviceManager, 0, 0, 2, 2, true,
                                                                            false, true, false);
            addAndMakeVisible(*devices_);
            devices_->setSize(kWidth - 2 * kPadX, 600);  // it shrinks itself to what it shows
        }
#endif
    startTimer(1000);
    refresh();
    setSize(kWidth, layout());
}

SettingsPage::~SettingsPage() { setLookAndFeel(nullptr); }

void SettingsPage::refresh() {
    const auto dir = proc_.settings().romFolder();
    romField_.setText(dir == juce::File() ? juce::String("No folder chosen") : dir.getFullPathName(),
                      juce::dontSendNotification);
    roms_ = checkRomFolder(dir, nullptr);
    memField_.setText(proc_.settings().memoryFile().getFullPathName(), juce::dontSendNotification);
    memNote_ = proc_.memoryError();
}

void SettingsPage::timerCallback() {
    if (devices_ != nullptr) repaint(rateLine_);
    if (proc_.memoryError() != memNote_) changed();  // the 60 s save failed (or recovered) while open
}

void SettingsPage::changed() {
    refresh();
    setSize(getWidth(), layout());
    repaint();
    if (onChange) onChange();
}

int SettingsPage::layout() {
    const juce::ScopedValueSetter<bool> scope(laying_, true);
    const int x = kPadX, w = kWidth - 2 * kPadX;
    int y = kPadY;
    header_ = {x, y, w, 34};
    y += 34 + kGap;

    romsHead_ = {x, y, w, kHead};
    y += kHead + 10;
    romField_.setBounds(x, y, w - 130 - 8, kRow);
    chooseRoms_.setBounds(x + w - 130, y, 130, kRow);
    y += kRow + 10;
    const int rows = static_cast<int>(roms_.size());
    romList_ = {x, y, w, 20 + rows * 17 + (rows - 1) * 4};
    y += romList_.getHeight() + kGap;

    if (devices_ != nullptr) {
        devHead_ = {x, y, w, kHead};
        y += kHead + 10;
        devices_->setBounds(x, y, w, devices_->getHeight());
        y += devices_->getHeight();
        rateLine_ = {x, y, w, 16};
        y += 16 + kGap;
    }

    memHead_ = {x, y, w, kHead};
    y += kHead + 10;
    memLabel_ = {x, y, 120, kRow};
    const int fx = x + 132, cw = 100;
    memField_.setBounds(fx, y, w - 132 - cw - 12, kRow);
    changeMem_.setBounds(x + w - cw, y, cw, kRow);
    y += kRow + 8;
    memNoteArea_ = {fx, y, w - 132, memNote_.isEmpty() ? 16 : 34};
    y += memNoteArea_.getHeight() + 8;
    erase_.setBounds(fx, y, 140, kRow);
    return y + kRow + kPadY;
}

void SettingsPage::resized() { layout(); }

void SettingsPage::childBoundsChanged(juce::Component* child) {
    if (!laying_ && child != nullptr && child == devices_.get()) setSize(getWidth(), layout());
}

void SettingsPage::paint(juce::Graphics& g) {
    g.fillAll(kPanel);

    g.setColour(kText);
    g.setFont(font(26.0f, true));
    g.drawText("Settings", header_, juce::Justification::centredLeft);
    g.setColour(kDim);
    g.setFont(font(12.0f));
    g.drawText("Changes to ROMs or memory restart the machine", header_, juce::Justification::bottomRight);

    heading(g, "ROMS", romsHead_);
    g.setColour(kList);
    g.fillRoundedRectangle(romList_.toFloat(), 4.0f);
    g.setColour(kListEdge);
    g.drawRoundedRectangle(romList_.toFloat().reduced(0.5f), 4.0f, 1.0f);
    auto list = romList_.reduced(12, 10);
    const int col = (list.getWidth() - 22 - 70) / 2;
    for (const auto& r : roms_) {
        auto row = list.removeFromTop(17);
        list.removeFromTop(4);
        const bool ok = r.state == RomFileStatus::Found;
        const auto colour = ok ? kGood : kBad;
        g.setColour(colour);
        g.setFont(font(13.0f));
        g.drawText(juce::String::charToString(ok ? 0x2713 : 0x2715), row.removeFromLeft(22),
                   juce::Justification::centredLeft);
        g.drawText(ok ? "found" : r.state == RomFileStatus::Missing ? "missing" : "wrong size", row.removeFromRight(70),
                   juce::Justification::centredRight);
        g.setColour(kText);
        g.setFont(mono(12.0f));
        g.drawText(r.file, row.removeFromLeft(col), juce::Justification::centredLeft, true);
        g.setColour(kDim);
        g.setFont(font(13.0f));
        g.drawText(r.what, row, juce::Justification::centredLeft, true);
    }

    if (devices_ != nullptr) {
        heading(g, "AUDIO AND MIDI", devHead_);
        const auto* dev = devices_->deviceManager.getCurrentAudioDevice();
        const int xruns = dev != nullptr ? dev->getXRunCount() : -1;
        const auto rate = proc_.getSampleRate();
        g.setColour(kDim);
        g.setFont(font(12.0f));
        g.drawText("The KS-32 runs at " + hz(ens::ks32::kWavRate) + " Hz and is resampled to " +
                       (rate > 0 ? hz(rate) + " Hz." : juce::String("the device rate (no device open).")) +
                       " Underruns since start: " + (xruns >= 0 ? juce::String(xruns) : juce::String("not reported")),
                   rateLine_, juce::Justification::centredLeft, true);
    }

    heading(g, "MEMORY", memHead_);
    g.setColour(kBody);
    g.setFont(font(14.0f));
    g.drawText("Memory file", memLabel_, juce::Justification::centredLeft);
    auto note = memNoteArea_;
    g.setColour(kDim);
    g.setFont(font(12.0f));
    // DECISION A11: saved on close and every 60 s, once the machine has booted.
    g.drawText("Holds what the battery keeps: settings, edited sounds and sequences. Saved when the app "
               "closes and every minute.",
               note.removeFromTop(16), juce::Justification::centredLeft, true);
    if (memNote_.isNotEmpty()) {
        g.setColour(kBad);
        g.drawText(memNote_, note.withTrimmedTop(2), juce::Justification::centredLeft, true);
    }
}

void SettingsPage::chooseRomFolder() {
    chooser_ = std::make_unique<juce::FileChooser>("Choose the ROM folder", proc_.settings().romFolder());
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& fc) {
                              const auto dir = fc.getResult();
                              if (dir == juce::File()) return;  // cancelled
                              proc_.setRomFolder(dir);
                              changed();
                          });
}

void SettingsPage::chooseMemoryFile() {
    chooser_ = std::make_unique<juce::FileChooser>("Choose the memory file", proc_.settings().memoryFile(), "*.bin");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File()) return;  // cancelled
                              juce::String err;
                              if (!proc_.setMemoryFile(file, err))
                                  juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                         "Memory file", err);
                              changed();
                          });
}

void SettingsPage::confirmErase() {
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                             .withTitle("Erase memory")
                             .withMessage("This erases the settings, edited sounds and sequences the battery keeps, "
                                          "and restarts the machine cold. The memory file is erased now.")
                             .withButton("Erase")
                             .withButton("Cancel")
                             .withAssociatedComponent(this);
    juce::AlertWindow::showAsync(options, [safe = juce::Component::SafePointer<SettingsPage>(this)](int result) {
        if (safe == nullptr || result != 1) return;  // 1: Erase
        safe->proc_.eraseMemory();
        safe->changed();
    });
}
