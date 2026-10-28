#include "PanelEditor.h"

#include "Processor.h"
#include "SettingsPage.h"
#include "app/panel_layout.h"
#include "ks32/machine.h"

#if JucePlugin_Build_Standalone
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using ens::app::Control;
using ens::app::ControlKind;
using ens::app::Ink;

namespace {

// The artboard's colours.
const juce::Colour kBack{0xff0d0d0e}, kPanel{0xff1f2023}, kGroove{0xff08080a}, kLower{0xff141416},
    kWheelBox{0xff1b1c1f}, kEdge{0xff2a2b2e}, kLcd{0xffffb347}, kLcdBack{0xff050403}, kStrip{0xff5fb3a1},
    kRule{0xff8b8e94}, kButton{0xff3b3d42}, kButtonEdge{0xff55585e}, kButtonShade{0xff26272a},
    kButtonDown{0xff303236}, kLedOn{0xffff4a3d}, kLedOff{0xff4a2522}, kText{0xffc9cbcf}, kBright{0xffe6e7ea},
    kDim{0xffa7aab0}, kFocus{0xff9fd4c7}, kTrackEdge{0xff2c2e32}, kCap{0xff4a4c51}, kCapEdge{0xff6a6d73},
    kWhite{0xffecebe6}, kWhiteEdge{0xff9a9993}, kBlack{0xff1a1a1c}, kBlackShade{0xff2e2f33},
    kRidge{0xff2b2c30}, kRidgeLine{0xff38393e}, kWheelEdge{0xff4a4c51};

juce::Rectangle<int> rect(const ens::app::Rect& r) { return {r.x, r.y, r.w, r.h}; }

void box(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, juce::Colour edge, float radius,
         float edgeW = 1.0f) {
    g.setColour(fill);
    g.fillRoundedRectangle(r, radius);
    g.setColour(edge);
    g.drawRoundedRectangle(r.reduced(edgeW / 2), radius, edgeW);
}

// The artboard's raised button: a bottom shade, pressed buttons drawn sunk.
void drawButton(juce::Graphics& g, juce::Rectangle<float> r, bool down) {
    g.setColour(down ? kButtonDown : kButton);
    g.fillRoundedRectangle(r, 4.0f);
    if (!down) {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(r.withTop(r.getBottom() - 4).toNearestInt());
        g.setColour(kButtonShade);
        g.fillRoundedRectangle(r.reduced(1), 3.0f);
    }
    g.setColour(down ? kButtonShade : kButtonEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
}

void drawGear(juce::Graphics& g, juce::Rectangle<float> r) {
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.26f;
    juce::Path p;
    for (int i = 0; i < 8; ++i) {
        juce::Path tooth;
        tooth.addRectangle(c.x - rad * 0.28f, c.y - rad * 1.35f, rad * 0.56f, rad * 0.8f);
        p.addPath(tooth, juce::AffineTransform::rotation(juce::MathConstants<float>::pi * i / 4, c.x, c.y));
    }
    p.addEllipse(c.x - rad, c.y - rad, rad * 2, rad * 2);
    g.setColour(kText);
    g.fillPath(p);
    g.setColour(kButton);
    g.fillEllipse(c.x - rad * 0.42f, c.y - rad * 0.42f, rad * 0.84f, rad * 0.84f);
}

juce::Font labelFont(float height, bool bold) {
    return juce::Font(juce::FontOptions("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
}

// One control of the table as a component, in design units: tooltip, accessible name, role.
class Part : public juce::Component, public juce::SettableTooltipClient {
public:
    Part(PanelEditor& ed, const Control& c) : ed_(ed), c_(c) {
        setTitle(c.label);
        setTooltip(c.label);
    }

protected:
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        using R = juce::AccessibilityRole;
        const R role = c_.kind == ControlKind::Button || c_.kind == ControlKind::Key ? R::button
                       : c_.kind == ControlKind::Slider || c_.kind == ControlKind::Wheel ? R::slider
                       : c_.kind == ControlKind::Display                                   ? R::staticText
                                                                                           : R::image;
        return std::make_unique<juce::AccessibilityHandler>(*this, role, actions());
    }
    virtual juce::AccessibilityActions actions() { return {}; }

    PanelEditor& ed_;
    const Control& c_;
};

class ButtonPart : public Part {
public:
    ButtonPart(PanelEditor& ed, const Control& c) : Part(ed, c), gear_(c.id == ens::app::kSettingsId) {
        available_ = c.id >= 0 || gear_;
        if (!available_) {
            setAlpha(0.4f);
            setTooltip("Not available: code not found");
        }
        setWantsKeyboardFocus(available_);
        setBounds(rect(c.r));
    }

    // DECISION A9: a right-click or Shift-click latches the button down until it is clicked again; a
    // plain press is sent on mouse-down and its release on mouse-up, wherever the mouse is.
    void mouseDown(const juce::MouseEvent& e) override {
        if (!available_ || gear_) return;
        if (latched_) {
            latched_ = false;
            set(false);
        } else if (e.mods.isPopupMenu() || e.mods.isShiftDown()) {
            latched_ = true;
            set(true);
        } else {
            byMouse_ = true;
            set(true);
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (gear_) {
            if (getLocalBounds().contains(e.getPosition())) ed_.openSettings();
        } else if (byMouse_) {
            byMouse_ = false;
            if (!latched_) set(false);
        }
    }
    // Space presses the focused button while it is held.
    bool keyPressed(const juce::KeyPress& k) override {
        if (k != juce::KeyPress::spaceKey) return false;
        if (gear_) {
            ed_.openSettings();
        } else if (!down_) {
            byKey_ = true;
            set(true);
        }
        return true;
    }
    bool keyStateChanged(bool) override {
        if (!byKey_ || juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::spaceKey)) return false;
        byKey_ = false;
        if (!latched_) set(false);
        return true;
    }
    void focusLost(FocusChangeType) override {
        if (byKey_) release(false);
        repaint();
    }
    void focusGained(FocusChangeType) override { repaint(); }
    // DECISION A9: losing focus releases unlatched buttons. Closing the editor releases them all.
    void release(bool latchedToo) {
        byMouse_ = byKey_ = false;
        if (latchedToo) latched_ = false;
        if (!latched_) set(false);
    }

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        drawButton(g, r, down_);
        if (gear_) drawGear(g, r);
        if (hasKeyboardFocus(false)) {
            g.setColour(kFocus);
            g.drawRoundedRectangle(r.reduced(1.5f), 4.0f, 2.0f);
        }
    }

private:
    juce::AccessibilityActions actions() override {
        return juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] {
            if (gear_) {
                ed_.openSettings();
            } else if (available_ && !down_) {
                set(true);
                set(false);
            }
        });
    }
    void set(bool down) {
        if (down == down_) return;
        down_ = down;
        ed_.post(c_.id, down);
        repaint();
    }

    const bool gear_;
    bool available_ = true, down_ = false, latched_ = false, byMouse_ = false, byKey_ = false;
};

class LedPart : public Part {
public:
    LedPart(PanelEditor& ed, const Control& c) : Part(ed, c) {
        setTitle(juce::String(c.label) + " LED");
        setTooltip(getTitle());
        setBounds(rect(c.r).expanded(4));  // room for the glow
    }
    void paint(juce::Graphics& g) override {
        const auto led = ed_.snap().panel.leds[static_cast<size_t>(c_.id)];
        const bool on = ed_.romError().isEmpty() &&
                        (led == ens::ks32::Led::On || (led == ens::ks32::Led::Blink && ed_.blinkOn()));
        const auto r = getLocalBounds().toFloat().reduced(4);
        if (on) {
            g.setColour(kLedOn.withAlpha(0.35f));
            g.fillRoundedRectangle(r.expanded(3), 4.0f);
        }
        g.setColour(on ? kLedOn : kLedOff);
        g.fillRoundedRectangle(r, 2.0f);
    }
};

class DisplayPart : public Part {
public:
    DisplayPart(PanelEditor& ed, const Control& c) : Part(ed, c) {
        setTooltip("Display (right-click: View menu)");
        setBounds(rect(c.r));
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.addSectionHeader("View");
        m.addItem("Show display codes", true, ed_.hexCodes(), [&ed = ed_] { ed.toggleHexCodes(); });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
    }
    void paint(juce::Graphics& g) override {
        box(g, getLocalBounds().toFloat(), kLcdBack, kEdge, 6.0f, 2.0f);
        // The text area inside the artboard's 2 + 14 / 18 border and padding: rows 36 high.
        const float x0 = 20, y0 = 16, cell = (getWidth() - 40) / 16.0f, rowH = 36;
        const auto err = ed_.romError();
        const auto& p = ed_.snap().panel;
        g.setColour(kLcd);
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 16; ++col) {
                const juce::Rectangle<float> r(x0 + col * cell, y0 + row * rowH, cell, rowH);
                int code = ' ';
                if (err.isNotEmpty()) {
                    // No engine: the reason, two rows cut to 16 columns.
                    const auto line = juce::StringArray::fromLines(err)[row];
                    if (col < line.length()) code = static_cast<int>(line[col]) & 0x7F;
                } else {
                    code = static_cast<unsigned char>(p.rows[static_cast<size_t>(row)][static_cast<size_t>(col)]);
                    // DECISION A8: blinking cells blink at 2 Hz, 50 % duty (blank while off).
                    if ((p.blink >> (row * 16 + col)) & 1u && !ed_.blinkOn()) code = ' ';
                }
                if (ed_.hexCodes()) {
                    g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f,
                                                           juce::Font::plain)));
                    g.drawText(juce::String::toHexString(code).paddedLeft('0', 2).toUpperCase(), r,
                               juce::Justification::centred, false);
                } else if (code >= 0x20 && code <= 0x7E) {
                    // DECISION A7: 0x20-0x7E as ASCII; 0x00-0x1F and 0x7F blank (the overlay shows them).
                    g.setFont(juce::Font(juce::FontOptions("Consolas", 32.0f, juce::Font::plain)));
                    g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(code)), r,
                               juce::Justification::centred, false);
                }
            }
        }
    }
};

class SliderPart : public Part {
public:
    SliderPart(PanelEditor& ed, const Control& c) : Part(ed, c) { setBounds(rect(c.r).expanded(10, 0)); }
    void paint(juce::Graphics& g) override {
        const auto track = getLocalBounds().toFloat().reduced(10, 0);
        box(g, track, kGroove, kTrackEdge, 4.0f);
        // The cap at the resting value (K13); the artboard's travel is 18 (top) to 166 units.
        const float v = ens::ks32::kAnalogRest[static_cast<size_t>(c_.id)] / 1023.0f;
        const juce::Rectangle<float> cap(0, 19 + (1 - v) * 148, static_cast<float>(getWidth()), 32);
        box(g, cap, kCap, kCapEdge, 4.0f);
        g.setColour(juce::Colour(0xff303236));
        g.fillRect(cap.reduced(1).withTop(cap.getBottom() - 5));
    }
};

class WheelPart : public Part {
public:
    WheelPart(PanelEditor& ed, const Control& c) : Part(ed, c) { setBounds(rect(c.r)); }
    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        juce::Path shape;
        shape.addRoundedRectangle(r, 13.0f);
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(shape);
        g.fillAll(kRidge);
        g.setColour(kRidgeLine);
        for (float y = 5; y < r.getHeight(); y += 7) g.fillRect(0.0f, y, r.getWidth(), 2.0f);
        g.setColour(kWheelEdge);
        g.strokePath(shape, juce::PathStrokeType(2.0f));
    }
};

class KeyPart : public Part {
public:
    KeyPart(PanelEditor& ed, const Control& c) : Part(ed, c) {
        const int n = c.id % 12;
        black_ = n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
        setBounds(rect(c.r));
    }
    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        juce::Path shape;
        const float rad = black_ ? 3.0f : 4.0f;
        shape.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad, false, false, true,
                                  true);
        g.setColour(black_ ? kBlack : kWhite);
        g.fillPath(shape);
        if (black_) {
            g.setColour(kBlackShade);
            g.fillRect(r.reduced(1).withTop(r.getBottom() - 6));
        }
        g.setColour(black_ ? juce::Colours::black : kWhiteEdge);
        g.strokePath(shape, juce::PathStrokeType(1.0f));
    }

private:
    bool black_ = false;
};

// The artboard at its design size; the editor scales it with a transform.
class Board : public juce::Component {
public:
    explicit Board(PanelEditor& ed) : ed_(ed) {
        setSize(ens::app::kDesignW, ens::app::kDesignH);
        // Order: the keys as the table lists them (whites, then the blacks over them).
        for (const Control& c : ens::app::panelLayout()) {
            std::unique_ptr<Part> p;
            switch (c.kind) {
                case ControlKind::Button: {
                    auto b = std::make_unique<ButtonPart>(ed, c);
                    buttons_.push_back(b.get());
                    p = std::move(b);
                    break;
                }
                case ControlKind::Led: {
                    auto l = std::make_unique<LedPart>(ed, c);
                    leds_.push_back(l.get());
                    p = std::move(l);
                    break;
                }
                case ControlKind::Display:
                    p = std::make_unique<DisplayPart>(ed, c);
                    display_ = p.get();
                    break;
                case ControlKind::Slider: p = std::make_unique<SliderPart>(ed, c); break;
                case ControlKind::Wheel: p = std::make_unique<WheelPart>(ed, c); break;
                case ControlKind::Key: p = std::make_unique<KeyPart>(ed, c); break;
                case ControlKind::Label: labels_.push_back(&c); continue;
            }
            addAndMakeVisible(*p);
            parts_.push_back(std::move(p));
        }
    }

    void release(bool latchedToo) {
        for (auto* b : buttons_) b->release(latchedToo);
    }
    void repaintPanel() {
        display_->repaint();
        for (auto* l : leds_) l->repaint();
        repaint(32, 270, 120, 45);  // the status lines
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(kBack);
        g.setColour(kPanel);
        g.fillRect(0, 0, 2040, 374);
        g.setColour(kGroove);
        g.fillRect(0, 374, 2040, 6);
        g.setColour(kLower);
        g.fillRect(0, 380, 2040, 340);
        juce::Path wheels;
        wheels.addRoundedRectangle(32.0f, 398.0f, 150.0f, 296.0f, 6.0f, 6.0f, true, false, true, false);
        g.setColour(kWheelBox);
        g.fillPath(wheels);
        g.setColour(kEdge);
        g.strokePath(wheels, juce::PathStrokeType(1.0f));
        g.setColour(kGroove);
        g.fillRect(182, 398, 1826, 296);
        // The two outlined strips and the rules under the "Tracks" headings.
        g.setColour(kStrip);
        g.drawRoundedRectangle(830.5f, 50.5f, 573.0f, 37.0f, 3.0f, 1.0f);
        g.drawRoundedRectangle(830.5f, 95.5f, 573.0f, 37.0f, 3.0f, 1.0f);
        g.setColour(kRule);
        const float dash[] = {1.0f, 2.0f};
        g.drawDashedLine({1120.0f, 41.5f, 1286.0f, 41.5f}, dash, 2, 1.0f);
        g.drawLine(1607.0f, 49.5f, 1815.0f, 49.5f, 1.0f);

        for (const Control* c : labels_) {
            juce::String text = c->label;
            auto r = rect(c->r);
            auto just = juce::Justification::centred;
            if (text.startsWith("MIDI:")) {  // the live MIDI input in place of the artboard's sample
                text = "MIDI: " + ed_.midiInput();
                r.setWidth(120);
                just = juce::Justification::centredLeft;
            }
            g.setColour(c->ink == Ink::Bright ? kBright : c->ink == Ink::Dim ? kDim : kText);
            g.setFont(labelFont(static_cast<float>(c->r.h), c->ink == Ink::Bright));
            g.drawFittedText(text, r, just, 1, 0.5f);
        }
    }

private:
    PanelEditor& ed_;
    std::vector<std::unique_ptr<Part>> parts_;
    std::vector<ButtonPart*> buttons_;
    std::vector<LedPart*> leds_;
    std::vector<const Control*> labels_;
    juce::Component* display_ = nullptr;
};

// The settings page in its own window, as tall as the page within the screen; the rest scrolls.
class SettingsWindow : public juce::DocumentWindow, private juce::ComponentListener {
public:
    explicit SettingsWindow(std::unique_ptr<SettingsPage> page)
        : DocumentWindow("KS-32 Settings", kPanel, closeButton) {
        setUsingNativeTitleBar(true);
        page->addComponentListener(this);
        view_.setViewedComponent(page.release(), true);
        view_.setScrollBarsShown(true, false);
        setContentNonOwned(&view_, true);  // the window follows the view's size
        fit();
    }
    ~SettingsWindow() override {
        view_.getViewedComponent()->removeComponentListener(this);
        clearContentComponent();
    }
    void closeButtonPressed() override { setVisible(false); }

private:
    void componentMovedOrResized(juce::Component&, bool, bool wasResized) override {
        if (wasResized) fit();
    }
    void fit() {
        const auto* page = view_.getViewedComponent();
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        // The screen's height less the title bar.
        const int room =
            display != nullptr ? static_cast<int>(display->userBounds.getHeight()) - 48 : page->getHeight();
        const int h = std::min(page->getHeight(), room);
        view_.setSize(page->getWidth() + (h < page->getHeight() ? view_.getScrollBarThickness() : 0), h);
    }

    juce::Viewport view_;
};

Board& board(juce::Component& c) { return static_cast<Board&>(c); }

}  // namespace

PanelEditor::PanelEditor(Ks32Processor& p) : AudioProcessorEditor(p), proc_(p) {
    if (auto* e = proc_.engine()) snap_ = e->snapshot();
    midiInput_ = findMidiInput();
    board_ = std::make_unique<Board>(*this);
    addAndMakeVisible(*board_);
    // DECISION A13: 1360 x 480 by default, resizable from 1020 x 360 up, aspect fixed at 2040:720.
    setResizable(true, true);
    setResizeLimits(1020, 360, 4080, 1440);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(ens::app::kDesignW) / ens::app::kDesignH);
    setSize(1360, 480);
    startTimerHz(30);
}

PanelEditor::~PanelEditor() { board(*board_).release(true); }

void PanelEditor::resized() {
    const float s = std::min(getWidth() / static_cast<float>(ens::app::kDesignW),
                             getHeight() / static_cast<float>(ens::app::kDesignH));
    board_->setTransform(juce::AffineTransform::scale(s));
}

void PanelEditor::post(int buttonCode, bool down) {
    if (auto* e = proc_.engine())
        e->post({ens::app::Event::Kind::Button, static_cast<uint8_t>(buttonCode), down, 0});
}

juce::String PanelEditor::romError() const { return proc_.engine() ? juce::String() : proc_.romError(); }

void PanelEditor::toggleHexCodes() {
    hexCodes_ = !hexCodes_;
    board(*board_).repaintPanel();
}

void PanelEditor::openSettings() {
    if (settings_ == nullptr) {
        auto page = std::make_unique<SettingsPage>(proc_);
        page->onChange = [this] { board_->repaint(); };
        settings_ = std::make_unique<SettingsWindow>(std::move(page));
        settings_->centreAroundComponent(this, settings_->getWidth(), settings_->getHeight());
    }
    settings_->setVisible(true);
    settings_->toFront(true);
}

juce::String PanelEditor::findMidiInput() const {
#if JucePlugin_Build_Standalone
    if (proc_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance()) {
            juce::StringArray names;
            for (const auto& d : juce::MidiInput::getAvailableDevices())
                if (holder->deviceManager.isMidiInputDeviceEnabled(d.identifier)) names.add(d.name);
            return names.isEmpty() ? juce::String("none") : names.joinIntoString(", ");
        }
#endif
    return "host";
}

void PanelEditor::timerCallback() {
    auto& b = board(*board_);
    const auto old = snap_;
    if (auto* e = proc_.engine()) snap_ = e->snapshot();
    // DECISION A8: blink at 2 Hz, 50 % duty, from the message-thread clock.
    const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    bool blinking = snap_.panel.blink != 0;
    for (auto l : snap_.panel.leds) blinking |= l == ens::ks32::Led::Blink;
    const bool changed = old.panel.rows != snap_.panel.rows || old.panel.blink != snap_.panel.blink ||
                         old.panel.leds != snap_.panel.leds || (blinking && blink != blinkOn_);
    blinkOn_ = blink;

    // DECISION A9: buttons held when the app loses the foreground are released (Alt-Tab while held).
    const bool fg = juce::Process::isForegroundProcess();
    if (foreground_ && !fg) b.release(false);
    foreground_ = fg;

    if (++ticks_ % 30 == 0) midiInput_ = findMidiInput();
    if (changed || ticks_ % 30 == 0) b.repaintPanel();

    // Debug: KS32_SNAPSHOT=<png path> writes the editor as it is drawn 3 s after it opens.
    if (ticks_ == 90) {
        const auto path = juce::SystemStats::getEnvironmentVariable("KS32_SNAPSHOT", {});
        if (path.isNotEmpty()) {
            const juce::File f(path);
            f.deleteFile();
            juce::FileOutputStream out(f);
            if (out.openedOk()) juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()), out);
        }
    }
}
