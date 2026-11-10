#pragma once
// L-DAW 0.6 mixer: channel strips, buses, FX rack (device style) + send FX panel
#include "Effects.h"
#include "Tools.h"
#include "EngineUI.h"

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    ~LevelMeter() override { detach(); }

    void attach (te::LevelMeterPlugin*);
    void detach();
    void tick();
    void paint (juce::Graphics&) override;

private:
    te::Plugin::Ptr meterPlugin;
    te::LevelMeasurer* measurer = nullptr;
    te::LevelMeasurer::Client client;
    float levels[2] { -100.0f, -100.0f }, peaks[2] { -100.0f, -100.0f };
    int peakHold[2] { 0, 0 };
};

//==============================================================================
class ParamKnob : public juce::Component
{
public:
    ParamKnob (juce::Colour, const juce::String& label, double defaultNorm);
    ~ParamKnob() override;

    std::function<te::AutomatableParameter* (bool create)> resolve;
    std::function<double()>              getter;
    std::function<void (double)>         setter;
    std::function<juce::String (double)> textFn;
    std::function<void()>                onRightClick;

    void tick();
    void setLabel (const juce::String&, juce::Colour);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::Slider slider;

private:
    juce::String label;
    juce::Colour labelColour;
    double defaultNorm;
};

//==============================================================================
struct SlotButton : public juce::TextButton
{
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown (e);
    }
};

//==============================================================================
class MixerStrip : public juce::Component
{
public:
    enum class Kind { music, bus, fxReturn, master };
    enum Section { secDyn = 0, secEq, secInserts, secSends, numSections };

    MixerStrip (te::Edit&, te::AudioTrack*, Kind, int index, juce::Colour);

    static int sectionHeight (int section);
    static constexpr int headerHeight = 24;
    static constexpr int bottomMinHeight = 236;

    void setSections (const bool visible[numSections]);
    void setSelected (bool);
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    te::PluginList& chain();
    juce::String channelName() const;
    juce::Colour getColour() const { return colour; }

    te::AudioTrack* track;
    const Kind kind;

    std::function<void (MixerStrip&, te::Plugin*)> onSelect;

private:
    te::Edit& edit;
    const int index;
    const juce::Colour colour;
    bool selected = false;
    bool show[numSections] { true, true, true, true };
    juce::Rectangle<int> sectionArea[numSections], headerArea, bottomArea;

    juce::Label nameLabel;
    juce::TextButton instrumentButton, outButton, dynOn { "ON" }, eqOn { "ON" }, addFxButton { "+ FX" },
                     muteButton { "M" }, soloButton { "S" };
    juce::OwnedArray<ParamKnob> dynKnobs, eqKnobs, sendKnobs;
    juce::OwnedArray<SlotButton> insertButtons;
    ParamKnob pan { juce::Colour (0xff6b6f78), "PAN", 0.5 };
    juce::Slider fader;
    LevelMeter meter;
    juce::String insertSignature;

    bool hasSection (int s) const;
    bool hasOutput() const { return kind == Kind::music || kind == Kind::bus; }
    te::VolumeAndPanPlugin* volume();
    void rebuildInserts();
    void showInsertMenu (te::Plugin*, juce::Component*);
    void showSendMenu (int bus);
    void showBusMenu();
    juce::String makeSignature();
    void toggleChannelPlugin (const juce::String& which);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerStrip)
};

//==============================================================================
class FxRackPanel : public juce::Component
{
public:
    FxRackPanel();
    ~FxRackPanel() override;

    void setChannel (te::Edit*, te::PluginList*, const juce::String& name, juce::Colour);
    void clear();
    void selectPlugin (te::Plugin*);
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row;
    te::Edit* edit = nullptr;
    te::PluginList* list = nullptr;
    juce::String title;
    juce::Colour colour;
    te::Plugin::Ptr selected, editorPlugin;

    juce::Component rowsContent;
    juce::Viewport rowsView, editorView;
    juce::OwnedArray<Row> rows;
    juce::TextButton addButton { "+ Add effect" }, windowButton { "Open window" };
    std::unique_ptr<juce::Component> editor;
    juce::String signature;

    void rebuild();
    void rebuildAsync();
    juce::String makeSignature() const;
    void showEditorFor (te::Plugin*);
};

//==============================================================================
class SendsPanel : public juce::Component
{
public:
    SendsPanel();
    ~SendsPanel() override;

    void setEdit (te::Edit*);
    void tick();
    std::function<void (int bus)> onEdit;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row;
    te::Edit* edit = nullptr;
    juce::OwnedArray<Row> rows;
};

//==============================================================================
class MixerView : public juce::Component
{
public:
    MixerView();
    ~MixerView() override;

    void setEdit (te::Edit*);
    void rebuild();
    void tick();
    void showInRack (te::EditItemID trackID);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    te::Edit* edit = nullptr;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<MixerStrip> strips;
    juce::String trackSignature;

    FxRackPanel rack;
    SendsPanel sends;
    juce::TextButton rackTab { "FX RACK" }, sendsTab { "SEND FX" }, panelButton { "FX" }, addBusButton { "+ BUS" };
    bool panelShown = true, showSends = false;

    te::EditItemID rackTrackID;
    bool userShow[MixerStrip::numSections] { true, true, true, true };
    juce::TextButton sectionButtons[MixerStrip::numSections];

    juce::String makeTrackSignature();
    void selectStrip (MixerStrip&, te::Plugin*);
    void selectBus (int bus);
    void restoreRackSelection();
    void setTab (bool sendsTab);
};
