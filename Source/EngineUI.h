#pragma once
// L-DAW 0.6: built-in effects shown as hardware-style devices
#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
// Device faceplate for the built-in effects (Reason-style rack device):
// rack ears with screws, LCD name display, power LED switch, knob grid.
// Knob gestures are reported so moves can be recorded as automation.
// (Class name kept as GenericPluginEditor so the rest of the app is unchanged.)
//==============================================================================
class GenericPluginEditor : public juce::Component,
                            private juce::Timer
{
public:
    explicit GenericPluginEditor (te::Plugin& p) : plugin (&p)
    {
        name  = p.state.getProperty ("ldawFxName").toString();
        if (name.isEmpty()) name = p.getName();
        accent = colourFor (name + " " + p.getName());
        enabled = p.isEnabled();

        power.setClickingTogglesState (true);
        power.setToggleState (p.isEnabled(), juce::dontSendNotification);
        power.setTooltip ("Power (bypass)");
        power.onClick = [this] { if (plugin != nullptr) plugin->setEnabled (power.getToggleState()); };
        power.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3fbf6a));
        power.setButtonText ("");
        addAndMakeVisible (power);

        for (int i = 0; i < p.getNumAutomatableParameters(); ++i)
        {
            te::AutomatableParameter::Ptr param (p.getAutomatableParameter (i));
            if (param == nullptr) continue;

            auto* k = knobs.add (new Knob());
            k->param = param;
            k->label = shortLabel (param->getParameterName());
            k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k->slider.setRange (0.0, 1.0, 0.0);
            k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
            k->slider.setValue (param->getCurrentNormalisedValue(), juce::dontSendNotification);
            k->slider.setDoubleClickReturnValue (true, param->getCurrentNormalisedValue());
            k->slider.setTooltip (param->getParameterName());

            auto* kp = k;
            k->slider.onDragStart = [kp] { kp->param->parameterChangeGestureBegin(); };
            k->slider.onDragEnd   = [kp] { kp->param->parameterChangeGestureEnd(); };
            k->slider.onValueChange = [this, kp]
            {
                kp->param->setNormalisedParameter ((float) kp->slider.getValue(), juce::sendNotification);
                kp->value = kp->param->getCurrentValueAsString();
                repaint();
            };
            k->value = param->getCurrentValueAsString();
            addAndMakeVisible (k->slider);
        }

        if (auto* d = dynamic_cast<te::DelayPlugin*> (&p))
        {
            delay = d;
            delayTime.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            delayTime.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            delayTime.setRange (10.0, 2000.0, 1.0);
            delayTime.setSkewFactorFromMidPoint (400.0);
            delayTime.setColour (juce::Slider::rotarySliderFillColourId, accent);
            delayTime.setValue ((double) d->lengthMs.get(), juce::dontSendNotification);
            delayTime.setTooltip ("Delay time (ms)");
            delayTime.onValueChange = [this] { if (delay != nullptr) delay->lengthMs = (int) delayTime.getValue(); repaint(); };
            addAndMakeVisible (delayTime);

            const char* names[] = { "1/4", "1/8", "1/8.", "1/16" };
            const double beats[] = { 1.0, 0.5, 0.75, 0.25 };
            for (int i = 0; i < 4; ++i)
            {
                auto* b = syncButtons.add (new juce::TextButton (names[i]));
                const double beatLen = beats[i];
                b->setTooltip ("Set delay time to " + juce::String (names[i]) + " note at the project tempo");
                b->onClick = [this, beatLen]
                {
                    if (delay == nullptr) return;
                    double bpm = 120.0;
                    if (auto t = delay->edit.tempoSequence.getTempo (0)) bpm = t->getBpm();
                    delayTime.setValue (std::round (beatLen * 60000.0 / bpm));
                };
                addAndMakeVisible (b);
            }
        }

        setSize (460, preferredHeight());
        startTimerHz (12);
    }

    int preferredHeight() const
    {
        const int items = knobs.size() + (delay != nullptr ? 1 : 0);
        const int rows = juce::jmax (1, (items + columns - 1) / columns);
        return headerH + rows * cellH + (delay != nullptr ? 34 : 0) + 18;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        // rack ears
        g.setColour (juce::Colour (0xff141518));
        g.fillRect (r);
        auto ears = r;
        auto leftEar  = ears.removeFromLeft (earW);
        auto rightEar = ears.removeFromRight (earW);
        for (auto ear : { leftEar, rightEar })
        {
            juce::ColourGradient eg (juce::Colour (0xff3a3c42), ear.getX(), 0.0f, juce::Colour (0xff24262a), ear.getRight(), 0.0f, false);
            g.setGradientFill (eg);
            g.fillRect (ear.reduced (0.0f, 1.0f));
            drawScrew (g, { ear.getCentreX(), ear.getY() + 14.0f });
            drawScrew (g, { ear.getCentreX(), ear.getBottom() - 14.0f });
        }

        // faceplate
        auto face = ears.reduced (2.0f, 2.0f);
        juce::ColourGradient fg (juce::Colour (0xff34363c), 0.0f, face.getY(), juce::Colour (0xff222327), 0.0f, face.getBottom(), false);
        g.setGradientFill (fg);
        g.fillRoundedRectangle (face, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (face, 3.0f, 1.0f);

        // accent stripe + brand
        g.setColour (accent);
        g.fillRect (face.getX() + 6.0f, face.getY() + 6.0f, 4.0f, (float) headerH - 16.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText ("L-DAW  FX", juce::Rectangle<float> (face.getX() + 16.0f, face.getY() + 6.0f, 80.0f, 12.0f),
                    juce::Justification::centredLeft);

        // LCD display
        auto lcd = juce::Rectangle<float> (face.getX() + 16.0f, face.getY() + 19.0f, juce::jmin (220.0f, face.getWidth() - 90.0f), 20.0f);
        g.setColour (juce::Colour (0xff0b1a10));
        g.fillRoundedRectangle (lcd, 2.0f);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (lcd, 2.0f, 1.0f);
        g.setColour (juce::Colour (0xff8df08a).withAlpha (enabled ? 1.0f : 0.35f));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold)));
        g.drawText (name.toUpperCase(), lcd.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);

        // power label + LED
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText ("POWER", power.getBounds().toFloat().translated (0.0f, -12.0f).withHeight (11.0f), juce::Justification::centred);
        auto led = juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ (float) power.getRight() + 9.0f, (float) power.getBounds().getCentreY() });
        g.setColour (enabled ? juce::Colour (0xff4cff6a) : juce::Colour (0xff2a3a2c));
        g.fillEllipse (led.expanded (1.0f));

        // knob labels + value windows
        for (auto* k : knobs)
            drawKnobCaption (g, k->slider.getBounds(), k->label, k->value);

        if (delay != nullptr)
            drawKnobCaption (g, delayTime.getBounds(), "TIME", juce::String ((int) delayTime.getValue()) + " ms");

        // divider under the header
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawHorizontalLine (headerH, face.getX() + 6.0f, face.getRight() - 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawHorizontalLine (headerH + 1, face.getX() + 6.0f, face.getRight() - 6.0f);
    }

    void resized() override
    {
        auto face = getLocalBounds().reduced (earW + 2, 2);
        power.setBounds (face.getRight() - 52, face.getY() + 20, 30, 18);

        auto body = face.withTrimmedTop (headerH + 6).reduced (8, 0);
        const int cw = juce::jmax (60, body.getWidth() / columns);

        int index = 0;
        auto place = [&] (juce::Component& c)
        {
            const int col = index % columns, row = index / columns;
            auto cell = juce::Rectangle<int> (body.getX() + col * cw, body.getY() + row * cellH, cw, cellH);
            c.setBounds (cell.withTrimmedTop (16).withTrimmedBottom (18).withSizeKeepingCentre (juce::jmin (54, cw - 8), 50));
            ++index;
        };

        for (auto* k : knobs) place (k->slider);
        if (delay != nullptr)
        {
            place (delayTime);
            const int rows = (index + columns - 1) / columns;
            auto b = juce::Rectangle<int> (body.getX(), body.getY() + rows * cellH + 4, body.getWidth(), 24);
            const int bw = juce::jmin (60, b.getWidth() / 4);
            for (auto* sb : syncButtons)
                sb->setBounds (b.removeFromLeft (bw).reduced (2, 0));
        }
    }

private:
    struct Knob
    {
        te::AutomatableParameter::Ptr param;
        juce::Slider slider;
        juce::String label, value;
    };

    static constexpr int columns = 4;
    static constexpr int headerH = 48;
    static constexpr int cellH   = 90;
    static constexpr int earW    = 16;

    te::Plugin::Ptr plugin;
    te::DelayPlugin* delay = nullptr;
    juce::String name;
    juce::Colour accent;
    bool enabled = true;

    juce::TextButton power;
    juce::OwnedArray<Knob> knobs;
    juce::Slider delayTime;
    juce::OwnedArray<juce::TextButton> syncButtons;

    static juce::Colour colourFor (const juce::String& n)
    {
        const auto s = n.toLowerCase();
        if (s.contains ("reverb"))                       return juce::Colour (0xff4a8ff0);
        if (s.contains ("delay"))                        return juce::Colour (0xff36b3a8);
        if (s.contains ("chorus"))                       return juce::Colour (0xffb36ad6);
        if (s.contains ("phaser"))                       return juce::Colour (0xffd84aa0);
        if (s.contains ("maxim"))                        return juce::Colour (0xffe0473a);
        if (s.contains ("comp") || s.contains ("dyn"))   return juce::Colour (0xffe8892a);
        if (s.contains ("eq") || s.contains ("equal"))   return juce::Colour (0xff58b85c);
        if (s.contains ("filter") || s.contains ("pass")) return juce::Colour (0xffe0c341);
        return juce::Colour (0xffe8752a);
    }

    static juce::String shortLabel (const juce::String& n)
    {
        auto s = n.toUpperCase().replace ("FREQUENCY", "FREQ").replace ("THRESHOLD", "THRESH")
                                .replace ("RESONANCE", "RES").replace ("FEEDBACK", "FDBK")
                                .replace ("RELEASE", "REL").replace ("ATTACK", "ATK").replace (" LEVEL", "");
        return s.length() > 11 ? s.substring (0, 11) : s;
    }

    static void drawScrew (juce::Graphics& g, juce::Point<float> c)
    {
        g.setColour (juce::Colour (0xff15161a));
        g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
        juce::ColourGradient sg (juce::Colour (0xffb9bcc2), c.x - 3.0f, c.y - 3.0f, juce::Colour (0xff5d6066), c.x + 3.0f, c.y + 3.0f, false);
        g.setGradientFill (sg);
        g.fillEllipse (c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colour (0xff2a2b2f));
        g.drawLine (c.x - 2.8f, c.y + 1.2f, c.x + 2.8f, c.y - 1.2f, 1.4f);
    }

    void drawKnobCaption (juce::Graphics& g, juce::Rectangle<int> knob, const juce::String& label, const juce::String& value)
    {
        auto top = juce::Rectangle<float> ((float) knob.getCentreX() - 34.0f, (float) knob.getY() - 16.0f, 68.0f, 13.0f);
        g.setColour (juce::Colour (0xff0b0c0e));
        g.fillRoundedRectangle (top, 2.0f);
        g.setColour (accent.brighter (0.4f).withAlpha (enabled ? 1.0f : 0.4f));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain)));
        g.drawText (value, top.reduced (2.0f, 0.0f), juce::Justification::centred, true);

        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (label, juce::Rectangle<float> ((float) knob.getCentreX() - 40.0f, (float) knob.getBottom() + 2.0f, 80.0f, 12.0f),
                    juce::Justification::centred, true);
    }

    void timerCallback() override
    {
        bool changed = false;
        for (auto* k : knobs)
            if (! k->slider.isMouseButtonDown())
            {
                const double v = k->param->getCurrentNormalisedValue();
                if (std::abs (v - k->slider.getValue()) > 1.0e-4)
                {
                    k->slider.setValue (v, juce::dontSendNotification);
                    k->value = k->param->getCurrentValueAsString();
                    changed = true;
                }
            }

        if (plugin != nullptr && plugin->isEnabled() != enabled)
        {
            enabled = plugin->isEnabled();
            power.setToggleState (enabled, juce::dontSendNotification);
            changed = true;
        }
        if (changed) repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenericPluginEditor)
};

//==============================================================================
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (te::PluginWindowState& s, te::Plugin& p)
        : juce::DocumentWindow (p.getName(), juce::Colour (0xff1b1c1f),
                                juce::DocumentWindow::closeButton, true),
          state (s)
    {
        setUsingNativeTitleBar (true);

        juce::Component* content = nullptr;

        if (auto* ep = dynamic_cast<te::ExternalPlugin*> (&p))
        {
            if (auto* inst = ep->getAudioPluginInstance())
                content = inst->hasEditor() ? static_cast<juce::Component*> (inst->createEditorIfNeeded())
                                            : static_cast<juce::Component*> (new juce::GenericAudioProcessorEditor (*inst));
        }
        else
        {
            content = new GenericPluginEditor (p);
        }

        if (content == nullptr)
        {
            auto* l = new juce::Label ({}, "No editor available for " + p.getName());
            l->setJustificationType (juce::Justification::centred);
            l->setSize (360, 80);
            content = l;
        }

        setContentOwned (content, true);

        static int cascade = 0;
        cascade = (cascade + 1) % 8;
        setTopLeftPosition (120 + cascade * 30, 120 + cascade * 30);
    }

    ~PluginWindow() override
    {
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        state.closeWindowExplicitly();
    }

private:
    te::PluginWindowState& state;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};

//==============================================================================
struct AppUIBehaviour : public te::UIBehaviour
{
    std::unique_ptr<juce::Component> createPluginWindow (te::PluginWindowState& s) override
    {
        if (auto* ws = dynamic_cast<te::Plugin::WindowState*> (&s))
            return std::make_unique<PluginWindow> (s, ws->plugin);

        return {};
    }
};
