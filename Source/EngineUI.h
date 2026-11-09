#pragma once
// L-DAW 0.5
#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
// Editor for the built-in effects: one slider per parameter + bypass.
// Slider gestures are reported so moves can be recorded as automation.
//==============================================================================
class GenericPluginEditor : public juce::Component,
                            private juce::Timer
{
public:
    explicit GenericPluginEditor (te::Plugin& p) : plugin (&p)
    {
        bypass.setButtonText ("On");
        bypass.setClickingTogglesState (true);
        bypass.setToggleState (p.isEnabled(), juce::dontSendNotification);
        bypass.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe8752a));
        bypass.onClick = [this] { if (plugin != nullptr) plugin->setEnabled (bypass.getToggleState()); };
        addAndMakeVisible (bypass);

        for (int i = 0; i < p.getNumAutomatableParameters(); ++i)
        {
            te::AutomatableParameter::Ptr param (p.getAutomatableParameter (i));
            if (param == nullptr) continue;

            auto* row = rows.add (new Row());
            row->param = param;
            row->name.setText (param->getParameterName(), juce::dontSendNotification);
            row->name.setColour (juce::Label::textColourId, juce::Colour (0xffe6e6e6));
            row->value.setColour (juce::Label::textColourId, juce::Colour (0xffe8752a));
            row->value.setJustificationType (juce::Justification::centredRight);
            row->slider.setSliderStyle (juce::Slider::LinearHorizontal);
            row->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            row->slider.setRange (0.0, 1.0, 0.0);
            row->slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffe8752a));
            row->slider.setValue (param->getCurrentNormalisedValue(), juce::dontSendNotification);

            auto* r = row;
            row->slider.onDragStart = [r] { r->param->parameterChangeGestureBegin(); };
            row->slider.onDragEnd   = [r] { r->param->parameterChangeGestureEnd(); };
            row->slider.onValueChange = [r]
            {
                r->param->setNormalisedParameter ((float) r->slider.getValue(), juce::sendNotification);
                r->value.setText (r->param->getCurrentValueAsString(), juce::dontSendNotification);
            };
            row->value.setText (param->getCurrentValueAsString(), juce::dontSendNotification);

            addAndMakeVisible (row->name);
            addAndMakeVisible (row->slider);
            addAndMakeVisible (row->value);
        }

        if (auto* d = dynamic_cast<te::DelayPlugin*> (&p))
        {
            delayLabel.setText ("Time (ms)", juce::dontSendNotification);
            delayLabel.setColour (juce::Label::textColourId, juce::Colour (0xffe6e6e6));
            delayTime.setSliderStyle (juce::Slider::LinearHorizontal);
            delayTime.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
            delayTime.setRange (10.0, 2000.0, 1.0);
            delayTime.setSkewFactorFromMidPoint (400.0);
            delayTime.setValue ((double) d->lengthMs.get(), juce::dontSendNotification);
            delayTime.onValueChange = [this, d] { d->lengthMs = (int) delayTime.getValue(); };
            addAndMakeVisible (delayLabel);
            addAndMakeVisible (delayTime);

            const char* names[] = { "1/4", "1/8", "1/8.", "1/16" };
            const double beats[] = { 1.0, 0.5, 0.75, 0.25 };
            for (int i = 0; i < 4; ++i)
            {
                auto* b = syncButtons.add (new juce::TextButton (names[i]));
                const double beatLen = beats[i];
                b->onClick = [this, d, beatLen]
                {
                    double bpm = 120.0;
                    if (auto t = d->edit.tempoSequence.getTempo (0)) bpm = t->getBpm();
                    delayTime.setValue (std::round (beatLen * 60000.0 / bpm));
                    d->lengthMs = (int) delayTime.getValue();
                };
                addAndMakeVisible (b);
            }
        }

        const int extra = syncButtons.isEmpty() ? 0 : 64;
        setSize (420, 44 + rows.size() * 30 + extra);
        startTimerHz (10);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff26272b));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        bypass.setBounds (r.removeFromTop (26).removeFromLeft (60));
        r.removeFromTop (8);

        const int nameW = juce::jmin (130, getWidth() / 3);
        for (auto* row : rows)
        {
            auto line = r.removeFromTop (30);
            row->name.setBounds (line.removeFromLeft (nameW));
            row->value.setBounds (line.removeFromRight (70));
            row->slider.setBounds (line);
        }

        if (! syncButtons.isEmpty())
        {
            auto line = r.removeFromTop (30);
            delayLabel.setBounds (line.removeFromLeft (nameW));
            delayTime.setBounds (line);
            auto b = r.removeFromTop (30).withTrimmedLeft (nameW);
            for (auto* sb : syncButtons)
                sb->setBounds (b.removeFromLeft (52).reduced (2));
        }
    }

private:
    struct Row
    {
        te::AutomatableParameter::Ptr param;
        juce::Label name, value;
        juce::Slider slider;
    };

    te::Plugin::Ptr plugin;
    juce::TextButton bypass;
    juce::OwnedArray<Row> rows;
    juce::Label delayLabel;
    juce::Slider delayTime;
    juce::OwnedArray<juce::TextButton> syncButtons;

    void timerCallback() override
    {
        for (auto* row : rows)
            if (! row->slider.isMouseButtonDown())
            {
                row->slider.setValue (row->param->getCurrentNormalisedValue(), juce::dontSendNotification);
                row->value.setText (row->param->getCurrentValueAsString(), juce::dontSendNotification);
            }
        if (plugin != nullptr)
            bypass.setToggleState (plugin->isEnabled(), juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenericPluginEditor)
};

//==============================================================================
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (te::PluginWindowState& s, te::Plugin& p)
        : juce::DocumentWindow (p.getName(), juce::Colour (0xff2a2b2f),
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
