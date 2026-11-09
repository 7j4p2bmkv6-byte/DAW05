#pragma once
// L-DAW 0.5
#include "Theme.h"

enum class EditTool { select, draw, erase, slice };

//==============================================================================
// Piano roll grid (inside a Viewport for vertical scrolling)
//==============================================================================
class NoteGrid : public juce::Component
{
public:
    static constexpr int   keyWidth = 48;
    static constexpr float noteHeight = 12.0f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    float noteToY (int note) const { return (127 - note) * noteHeight; }

    void selectAll();
    void deselectAll();
    void deleteSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void duplicateSelection();
    void quantize();
    void transpose (int semitones);
    void nudge (int steps);
    bool hasSelection() const;
    void clipChanged();

    te::Edit* edit = nullptr;
    te::EditItemID clipID;
    double snap = 0.25;
    EditTool tool = EditTool::draw;
    std::function<void()> onSelectionChanged;

private:
    enum class Mode { none, newNote, move, resize, velocity, marquee, erase };
    struct Orig { juce::ValueTree vt; double start, len; int pitch, vel; };

    Mode mode = Mode::none;
    std::vector<Orig> orig;
    juce::Array<juce::ValueTree> selection;
    juce::Point<float> downPos;
    juce::Rectangle<float> marquee;
    double downBeat = 0.0, lastLength = 0.25, pasteBeat = 0.0;

    te::MidiClip* clip() const { return ldaw::findMidiClip (edit, clipID); }
    double clipLengthBeats() const;
    double pixelsPerBeat() const;
    float  beatToX (double b) const   { return keyWidth + (float) (b * pixelsPerBeat()); }
    double xToBeat (float x) const    { return juce::jmax (0.0, (x - keyWidth) / pixelsPerBeat()); }
    int    yToNote (float y) const    { return juce::jlimit (0, 127, 127 - (int) (y / noteHeight)); }
    te::MidiNote* noteAt (juce::Point<float>, bool& onRightEdge) const;
    te::MidiNote* noteFor (const juce::ValueTree&) const;
    bool isSelected (te::MidiNote*) const;
    void pruneSelection();
    void captureOrig();
    void setSelection (juce::Array<juce::ValueTree>);
    std::vector<ldaw::ClipNote> collect (bool selectedOnly, double& base, double& span) const;
    void pasteNotes (const std::vector<ldaw::ClipNote>&, double at, double span);
    juce::UndoManager* um() const     { return edit != nullptr ? &edit->getUndoManager() : nullptr; }
};

//==============================================================================
// Drum step sequencer (16 steps per bar, one row per drum sound)
//==============================================================================
class StepGrid : public juce::Component
{
public:
    StepGrid();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    int numBars() const;

    void selectAll();
    void deselectAll();
    void deleteSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void duplicateSelection();
    bool hasSelection() const { return hasSel; }

    te::Edit* edit = nullptr;
    te::EditItemID clipID;
    int page = 0;
    EditTool tool = EditTool::draw;
    std::function<void()> onPageChanged;

    struct Lane { juce::String name; int note; };
    std::vector<Lane> lanes;

private:
    static constexpr int labelWidth = 130;
    int lastRow = -1, lastStep = -1;
    bool paintAdds = true;

    bool hasSel = false, selecting = false;
    int selRow0 = 0, selRow1 = 0, selStep0 = 0, selStep1 = 0, anchorRow = 0, anchorStep = 0;
    int cursorStep = 0;

    te::MidiClip* clip() const { return ldaw::findMidiClip (edit, clipID); }
    te::MidiNote* findStep (int row, int step) const;
    void setStep (int row, int step, bool on, bool accent);
    bool cellAt (juce::Point<int>, int& row, int& step) const;
    juce::Rectangle<float> cellBounds (int row, int step) const;
    float rowH() const;
    void showLaneMenu (int row);
    void fillRow (int row, int every);
    void clearRow (int row);
    double stepBeat (int step) const { return page * 4.0 + step * 0.25; }
};

//==============================================================================
class MidiEditorPanel : public juce::Component
{
public:
    MidiEditorPanel();
    ~MidiEditorPanel() override;

    void setClip (te::Edit*, te::EditItemID, bool stepMode);
    void tick();
    te::EditItemID getClipID() const   { return clipID; }
    bool isStepMode() const            { return stepMode; }

    bool handleKey (const juce::KeyPress&);
    bool wantsKeys = false;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onClose;

private:
    te::Edit* edit = nullptr;
    te::EditItemID clipID;
    bool stepMode = false;

    juce::TextButton pianoTab { "Piano roll" }, stepTab { "Step (drums)" },
                     prevBar { "<" }, nextBar { ">" }, closeButton { "Close" };

    juce::TextButton selectTool { "Select" }, drawTool { "Draw" }, eraseTool { "Erase" }, sliceTool { "Slice" };
    juce::TextButton cutButton { "Cut" }, copyButton { "Copy" }, pasteButton { "Paste" },
                     deleteButton { "Delete" }, dupButton { "Duplicate" }, quantizeButton { "Quantize" },
                     selectAllButton { "All" };

    juce::ComboBox   snapBox;
    juce::Label      clipLabel, barLabel, hintLabel;
    juce::Viewport   pianoView;
    NoteGrid         noteGrid;
    StepGrid         stepGrid;

    void setMode (bool step);
    void setTool (EditTool);
    void updateLabels();
};
