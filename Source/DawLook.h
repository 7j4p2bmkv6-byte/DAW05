#pragma once
// L-DAW 0.5 look: one dark theme for the whole app (header-only)
#include "Theme.h"

class DawLook : public juce::LookAndFeel_V4
{
public:
    DawLook() : juce::LookAndFeel_V4 (makeScheme())
    {
        using namespace ldaw;
        const juce::Colour well { 0xff121315 }, edge { 0xff3c3e44 };

        setColour (juce::ResizableWindow::backgroundColourId,        colours::background);
        setColour (juce::DocumentWindow::backgroundColourId,         colours::background);
        setColour (juce::TextButton::buttonColourId,                 colours::panelLight);
        setColour (juce::TextButton::buttonOnColourId,               colours::accent);
        setColour (juce::TextButton::textColourOffId,                colours::text);
        setColour (juce::TextButton::textColourOnId,                 juce::Colours::white);
        setColour (juce::ComboBox::backgroundColourId,               colours::panelLight);
        setColour (juce::ComboBox::outlineColourId,                  edge);
        setColour (juce::ComboBox::textColourId,                     colours::text);
        setColour (juce::ComboBox::arrowColourId,                    colours::dimText);
        setColour (juce::PopupMenu::backgroundColourId,              colours::panel);
        setColour (juce::PopupMenu::textColourId,                    colours::text);
        setColour (juce::PopupMenu::headerTextColourId,              colours::accent);
        setColour (juce::PopupMenu::highlightedBackgroundColourId,   colours::accent.withAlpha (0.85f));
        setColour (juce::PopupMenu::highlightedTextColourId,         juce::Colours::white);
        setColour (juce::Label::textColourId,                        colours::text);
        setColour (juce::Slider::thumbColourId,                      colours::accent);
        setColour (juce::Slider::trackColourId,                      colours::accent.withAlpha (0.6f));
        setColour (juce::Slider::backgroundColourId,                 well);
        setColour (juce::Slider::rotarySliderFillColourId,           colours::accent);
        setColour (juce::Slider::rotarySliderOutlineColourId,        well);
        setColour (juce::Slider::textBoxTextColourId,                colours::text);
        setColour (juce::Slider::textBoxBackgroundColourId,          well);
        setColour (juce::Slider::textBoxOutlineColourId,             juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId,                   juce::Colour (0xff4a4c52));
        setColour (juce::TooltipWindow::backgroundColourId,          colours::panelLight);
        setColour (juce::TooltipWindow::textColourId,                colours::text);
        setColour (juce::TooltipWindow::outlineColourId,             colours::accent.withAlpha (0.5f));
        setColour (juce::AlertWindow::backgroundColourId,            colours::panel);
        setColour (juce::AlertWindow::textColourId,                  colours::text);
        setColour (juce::TextEditor::backgroundColourId,             well);
        setColour (juce::TextEditor::textColourId,                   colours::text);
        setColour (juce::TextEditor::outlineColourId,                edge);
        setColour (juce::ListBox::backgroundColourId,                colours::panel);
    }

    static juce::LookAndFeel_V4::ColourScheme makeScheme()
    {
        using namespace ldaw;
        return { colours::background, colours::panel, colours::panel, juce::Colour (0xff3c3e44), colours::text,
                 colours::accent, juce::Colours::white, colours::accent, colours::text };
    }

    // coloured cap knob with value arc
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const float size = (float) juce::jmin (w, h) - 4.0f;
        const float cx = (float) x + (float) w * 0.5f, cy = (float) y + (float) h * 0.5f;
        const float radius = size * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        auto col = s.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff111214));
        g.strokePath (track, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (s.isEnabled())
        {
            juce::Path value;
            value.addCentredArc (cx, cy, radius, radius, 0.0f, startAngle, angle, true);
            g.setColour (col.withAlpha (0.9f));
            g.strokePath (value, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float skirt = radius * 0.80f;
        g.setColour (juce::Colour (0xff1a1b1e));
        g.fillEllipse (cx - skirt, cy - skirt, skirt * 2.0f, skirt * 2.0f);

        const float cap = radius * 0.62f;
        juce::ColourGradient grad (col.brighter (0.5f), cx - cap * 0.5f, cy - cap * 0.6f,
                                   col.darker (0.5f),   cx + cap * 0.6f, cy + cap * 0.8f, true);
        g.setGradientFill (grad);
        g.fillEllipse (cx - cap, cy - cap, cap * 2.0f, cap * 2.0f);

        juce::Path p;
        p.addRoundedRectangle (-1.2f, -skirt, 2.4f, skirt * 0.6f, 1.0f);
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (cx, cy));
        g.setColour (juce::Colours::white);
        g.fillPath (p);
    }

    // metal fader cap for vertical faders, flat style elsewhere
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearVertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
            return;
        }

        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float cx = r.getCentreX();

        g.setColour (juce::Colour (0xff0e0f11));
        g.fillRoundedRectangle (cx - 3.0f, r.getY(), 6.0f, r.getHeight(), 3.0f);
        g.setColour (s.findColour (juce::Slider::thumbColourId).withAlpha (0.55f));
        g.fillRoundedRectangle (cx - 1.5f, pos, 3.0f, juce::jmax (0.0f, r.getBottom() - pos), 1.5f);

        g.setColour (juce::Colour (0xff4a4d54));
        for (int i = 0; i <= 10; ++i)
        {
            const float ty = r.getY() + (float) i / 10.0f * r.getHeight();
            g.drawHorizontalLine ((int) ty, cx - 12.0f, cx - 6.0f);
            g.drawHorizontalLine ((int) ty, cx + 6.0f, cx + 12.0f);
        }

        const float capW = juce::jmin (r.getWidth() - 2.0f, 30.0f), capH = 26.0f;
        juce::Rectangle<float> capR (cx - capW * 0.5f, pos - capH * 0.5f, capW, capH);
        juce::ColourGradient grad (juce::Colour (0xffe9ebee), 0.0f, capR.getY(),
                                   juce::Colour (0xff7d8187), 0.0f, capR.getBottom(), false);
        grad.addColour (0.48, juce::Colour (0xffc2c5ca));
        grad.addColour (0.52, juce::Colour (0xff9da1a7));
        g.setGradientFill (grad);
        g.fillRoundedRectangle (capR, 3.0f);
        g.setColour (juce::Colour (0xff202124));
        g.drawRoundedRectangle (capR, 3.0f, 1.0f);
        g.setColour (s.findColour (juce::Slider::thumbColourId));
        g.fillRect (capR.getX() + 3.0f, capR.getCentreY() - 1.0f, capR.getWidth() - 6.0f, 2.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto base = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : background;
        if (down)             base = base.darker (0.2f);
        else if (highlighted) base = base.brighter (0.08f);

        juce::ColourGradient grad (base.brighter (0.10f), 0.0f, r.getY(), base.darker (0.10f), 0.0f, r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) buttonHeight * 0.6f), juce::Font::bold));
    }
};
