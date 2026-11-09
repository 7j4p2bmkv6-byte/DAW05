#include "MidiEditors.h"

using namespace ldaw;

namespace
{
    bool isBlackKey (int n)
    {
        const int k = n % 12;
        return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
    }

    juce::Colour velocityColour (int vel)
    {
        return colours::accent.withMultipliedBrightness (0.55f + 0.45f * (float) vel / 127.0f);
    }

    double clipBeats (te::Edit* e, te::MidiClip* c)
    {
        if (e == nullptr || c == nullptr) return 4.0;
        auto p = c->getPosition();
        return juce::jmax (0.25, beatOf (*e, p.getEnd()) - beatOf (*e, p.getStart()));
    }

    double playheadInClip (te::Edit* e, te::MidiClip* c)
    {
        if (e == nullptr || c == nullptr) return -1.0;
        const double now = beatOf (*e, e->getTransport().getPosition());
        const double s   = beatOf (*e, c->getPosition().getStart());
        const double rel = now - s;
        return (rel >= 0.0 && rel < clipBeats (e, c)) ? rel : -1.0;
    }

    double roundSpan (double raw, double snap)
    {
        const double unit = raw >= 1.0 ? 1.0 : snap;
        return juce::jmax (unit, std::ceil (raw / unit - 1e-6) * unit);
    }
}

//==============================================================================
// NoteGrid
//==============================================================================
double NoteGrid::clipLengthBeats() const { return clipBeats (edit, clip()); }
double NoteGrid::pixelsPerBeat() const   { return juce::jmax (1.0, (getWidth() - keyWidth) / clipLengthBeats()); }

te::MidiNote* NoteGrid::noteFor (const juce::ValueTree& vt) const
{
    auto* c = clip();
    if (c == nullptr || ! vt.isValid() || ! vt.getParent().isValid()) return nullptr;
    return c->getSequence().getNoteFor (vt);
}

bool NoteGrid::isSelected (te::MidiNote* n) const
{
    return n != nullptr && selection.contains (n->state);
}

bool NoteGrid::hasSelection() const
{
    for (auto& vt : selection)
        if (noteFor (vt) != nullptr) return true;
    return false;
}

void NoteGrid::pruneSelection()
{
    for (int i = selection.size(); --i >= 0;)
        if (noteFor (selection.getReference (i)) == nullptr)
            selection.remove (i);
}

void NoteGrid::setSelection (juce::Array<juce::ValueTree> s)
{
    selection = std::move (s);
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void NoteGrid::clipChanged()
{
    selection.clear();
    orig.clear();
    mode = Mode::none;
    pasteBeat = 0.0;
}

void NoteGrid::captureOrig()
{
    orig.clear();
    for (auto& vt : selection)
        if (auto* n = noteFor (vt))
            orig.push_back ({ vt, n->getStartBeat().inBeats(), n->getLengthBeats().inBeats(),
                              n->getNoteNumber(), n->getVelocity() });
}

te::MidiNote* NoteGrid::noteAt (juce::Point<float> p, bool& onRightEdge) const
{
    onRightEdge = false;
    auto* c = clip();
    if (c == nullptr) return nullptr;

    const int pitch = yToNote (p.y);
    auto& notes = c->getSequence().getNotes();

    for (int i = notes.size(); --i >= 0;)
    {
        auto* n = notes[i];
        if (n->getNoteNumber() != pitch) continue;

        const float x1 = beatToX (n->getStartBeat().inBeats());
        const float x2 = beatToX (n->getEndBeat().inBeats());
        if (p.x >= x1 && p.x < juce::jmax (x2, x1 + 4.0f))
        {
            onRightEdge = p.x > x2 - 6.0f;
            return n;
        }
    }
    return nullptr;
}

void NoteGrid::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    auto* c = clip();
    const float w = (float) getWidth();

    for (int n = 0; n < 128; ++n)
    {
        const float y = noteToY (n);
        g.setColour (isBlackKey (n) ? colours::background.darker (0.25f) : colours::background.brighter (0.04f));
        g.fillRect ((float) keyWidth, y, w - keyWidth, noteHeight);
        if (n % 12 == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawHorizontalLine ((int) (y + noteHeight) - 1, (float) keyWidth, w);
        }
    }

    const double len = clipLengthBeats();
    for (double b = 0.0; b <= len + 1e-6; b += snap)
    {
        const bool bar  = std::fmod (b, 4.0) < 1e-6;
        const bool beat = std::fmod (b, 1.0) < 1e-6;
        g.setColour (juce::Colours::white.withAlpha (bar ? 0.22f : beat ? 0.10f : 0.04f));
        g.drawVerticalLine ((int) beatToX (b), 0.0f, (float) getHeight());
    }

    if (c != nullptr)
    {
        for (auto* n : c->getSequence().getNotes())
        {
            const double s = n->getStartBeat().inBeats();
            const double l = n->getLengthBeats().inBeats();
            juce::Rectangle<float> r (beatToX (s), noteToY (n->getNoteNumber()) + 1.0f,
                                      juce::jmax (4.0f, (float) (l * pixelsPerBeat()) - 1.0f), noteHeight - 2.0f);
            const bool sel = isSelected (n);
            g.setColour (sel ? colours::selection : velocityColour (n->getVelocity()));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (sel ? juce::Colours::white : colours::accent.brighter (0.4f));
            g.drawRoundedRectangle (r, 2.0f, sel ? 1.5f : 0.8f);
        }

        if (pasteBeat >= 0.0 && pasteBeat < len)
        {
            g.setColour (colours::selection.withAlpha (0.6f));
            g.drawVerticalLine ((int) beatToX (pasteBeat), 0.0f, (float) getHeight());
        }

        const double ph = playheadInClip (edit, c);
        if (ph >= 0.0)
        {
            g.setColour (juce::Colours::white);
            g.drawVerticalLine ((int) beatToX (ph), 0.0f, (float) getHeight());
        }
    }

    if (mode == Mode::marquee)
    {
        g.setColour (colours::selection.withAlpha (0.12f));
        g.fillRect (marquee);
        g.setColour (colours::selection);
        g.drawRect (marquee, 1.0f);
    }

    for (int n = 0; n < 128; ++n)
    {
        const float y = noteToY (n);
        g.setColour (isBlackKey (n) ? juce::Colour (0xff111111) : juce::Colour (0xffe9e6df));
        g.fillRect (0.0f, y, (float) keyWidth, noteHeight - 1.0f);
        if (n % 12 == 0)
        {
            g.setColour (juce::Colours::black);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (juce::MidiMessage::getMidiNoteName (n, true, true, 3),
                        2, (int) y, keyWidth - 4, (int) noteHeight, juce::Justification::centredRight);
        }
    }

    if (c == nullptr)
    {
        g.setColour (colours::dimText);
        g.drawText ("Double-click a clip (or an empty spot in a track) to edit notes here.",
                    getLocalBounds().withTrimmedLeft (keyWidth).withHeight (400).withY ((int) noteToY (72)),
                    juce::Justification::centredTop);
    }
}

void NoteGrid::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    auto* n = noteAt (e.position, edge);

    if (tool == EditTool::erase)       setMouseCursor (juce::MouseCursor::CrosshairCursor);
    else if (tool == EditTool::slice)  setMouseCursor (n != nullptr ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
    else if (n == nullptr)             setMouseCursor (tool == EditTool::draw ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    else                               setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void NoteGrid::mouseDown (const juce::MouseEvent& e)
{
    mode = Mode::none;
    orig.clear();

    auto* c = clip();
    if (c == nullptr || e.x < keyWidth) return;

    pruneSelection();
    edit->getUndoManager().beginNewTransaction();

    downPos  = e.position;
    downBeat = xToBeat (e.position.x);

    bool edge = false;
    auto* n = noteAt (e.position, edge);

    if (tool == EditTool::erase || e.mods.isPopupMenu())
    {
        mode = Mode::erase;
        if (n != nullptr)
        {
            selection.removeAllInstancesOf (n->state);
            c->getSequence().removeNote (*n, um());
        }
        repaint();
        return;
    }

    if (tool == EditTool::slice)
    {
        if (n != nullptr)
        {
            const double s = n->getStartBeat().inBeats();
            const double end = n->getEndBeat().inBeats();
            double at = e.mods.isAltDown() ? downBeat : std::round (downBeat / snap) * snap;
            if (at > s + 1e-4 && at < end - 1e-4)
            {
                const int pitch = n->getNoteNumber(), vel = n->getVelocity();
                n->setStartAndLength (te::BeatPosition::fromBeats (s), te::BeatDuration::fromBeats (at - s), um());
                c->getSequence().addNote (pitch, te::BeatPosition::fromBeats (at),
                                          te::BeatDuration::fromBeats (end - at), vel, 0, um());
            }
        }
        repaint();
        return;
    }

    if (n == nullptr)
    {
        pasteBeat = juce::jmin (clipLengthBeats(), std::floor (downBeat / snap) * snap);

        if (tool == EditTool::draw)
        {
            const double len = clipLengthBeats();
            const double start = std::floor (downBeat / snap) * snap;
            if (start >= len) return;
            const double l = juce::jmin (lastLength, len - start);

            auto* nn = c->getSequence().addNote (yToNote (e.position.y), te::BeatPosition::fromBeats (start),
                                                 te::BeatDuration::fromBeats (l), 100, 0, um());
            if (nn != nullptr)
            {
                setSelection ({ nn->state });
                captureOrig();
                mode = Mode::newNote;
            }
        }
        else
        {
            if (! e.mods.isShiftDown()) setSelection ({});
            captureOrig();
            marquee = { e.position, e.position };
            mode = Mode::marquee;
        }
        repaint();
        return;
    }

    if (e.mods.isShiftDown() && tool == EditTool::select)
    {
        auto s = selection;
        if (isSelected (n)) s.removeAllInstancesOf (n->state); else s.add (n->state);
        setSelection (s);
        return;
    }

    if (! isSelected (n))
        setSelection ({ n->state });

    captureOrig();

    const bool velocityDrag = (tool == EditTool::draw && e.mods.isShiftDown())
                           || (tool == EditTool::select && e.mods.isCtrlDown());
    mode = velocityDrag ? Mode::velocity : edge ? Mode::resize : Mode::move;
    repaint();
}

void NoteGrid::mouseDrag (const juce::MouseEvent& e)
{
    auto* c = clip();
    if (c == nullptr || mode == Mode::none) return;

    const double len = clipLengthBeats();
    const double grid = e.mods.isAltDown() ? 1.0 / 64.0 : snap;

    if (mode == Mode::erase)
    {
        bool edge = false;
        if (auto* n = noteAt (e.position, edge))
        {
            selection.removeAllInstancesOf (n->state);
            c->getSequence().removeNote (*n, um());
        }
    }
    else if (mode == Mode::marquee)
    {
        marquee = juce::Rectangle<float> (downPos, e.position);
        juce::Array<juce::ValueTree> s;
        if (e.mods.isShiftDown())
            for (auto& o : orig) s.add (o.vt);

        for (auto* n : c->getSequence().getNotes())
        {
            juce::Rectangle<float> r (beatToX (n->getStartBeat().inBeats()), noteToY (n->getNoteNumber()),
                                      juce::jmax (4.0f, beatToX (n->getEndBeat().inBeats()) - beatToX (n->getStartBeat().inBeats())),
                                      noteHeight);
            if (r.intersects (marquee))
                s.addIfNotAlreadyThere (n->state);
        }
        selection = s;
    }
    else if (mode == Mode::move)
    {
        double delta = std::round ((xToBeat (e.position.x) - downBeat) / grid) * grid;
        int dPitch = (int) std::round (-e.getDistanceFromDragStartY() / noteHeight);

        double minStart = 1e9; int lo = 127, hi = 0;
        for (auto& o : orig)
        {
            minStart = juce::jmin (minStart, o.start);
            lo = juce::jmin (lo, o.pitch); hi = juce::jmax (hi, o.pitch);
        }
        delta  = juce::jlimit (-minStart, juce::jmax (-minStart, len - grid - minStart), delta);
        dPitch = juce::jlimit (-lo, 127 - hi, dPitch);

        for (auto& o : orig)
            if (auto* n = noteFor (o.vt))
            {
                n->setStartAndLength (te::BeatPosition::fromBeats (o.start + delta), te::BeatDuration::fromBeats (o.len), um());
                n->setNoteNumber (o.pitch + dPitch, um());
            }
    }
    else if (mode == Mode::newNote)
    {
        if (! orig.empty())
            if (auto* n = noteFor (orig.front().vt))
            {
                const double s = orig.front().start;
                const double end = std::round (xToBeat (e.position.x) / grid) * grid;
                const double l = juce::jlimit (grid, juce::jmax (grid, len - s), end - s);
                n->setStartAndLength (te::BeatPosition::fromBeats (s), te::BeatDuration::fromBeats (l), um());
                lastLength = l;
            }
    }
    else if (mode == Mode::resize)
    {
        const double delta = std::round ((xToBeat (e.position.x) - downBeat) / grid) * grid;
        for (auto& o : orig)
            if (auto* n = noteFor (o.vt))
            {
                const double l = juce::jlimit (grid, juce::jmax (grid, len - o.start), o.len + delta);
                n->setStartAndLength (te::BeatPosition::fromBeats (o.start), te::BeatDuration::fromBeats (l), um());
                if (orig.size() == 1) lastLength = l;
            }
    }
    else if (mode == Mode::velocity)
    {
        for (auto& o : orig)
            if (auto* n = noteFor (o.vt))
                n->setVelocity (juce::jlimit (1, 127, o.vel - e.getDistanceFromDragStartY() / 2), um());
    }
    repaint();
}

void NoteGrid::mouseUp (const juce::MouseEvent&)
{
    if (mode == Mode::marquee && onSelectionChanged) onSelectionChanged();
    mode = Mode::none;
    orig.clear();
    repaint();
}

std::vector<ClipNote> NoteGrid::collect (bool selectedOnly, double& base, double& span) const
{
    std::vector<ClipNote> out;
    auto* c = clip();
    base = 0.0; span = 0.0;
    if (c == nullptr) return out;

    double lo = 1e9, hi = 0.0;
    for (auto* n : c->getSequence().getNotes())
    {
        if (selectedOnly && ! isSelected (n)) continue;
        lo = juce::jmin (lo, n->getStartBeat().inBeats());
        hi = juce::jmax (hi, n->getEndBeat().inBeats());
    }
    if (lo > hi) return out;

    base = std::floor (lo / snap + 1e-6) * snap;
    span = roundSpan (hi - base, snap);

    for (auto* n : c->getSequence().getNotes())
        if (! selectedOnly || isSelected (n))
            out.push_back ({ n->getNoteNumber(), n->getStartBeat().inBeats() - base,
                             n->getLengthBeats().inBeats(), n->getVelocity() });
    return out;
}

void NoteGrid::pasteNotes (const std::vector<ClipNote>& notes, double at, double)
{
    auto* c = clip();
    if (c == nullptr) return;

    const double len = clipLengthBeats();
    juce::Array<juce::ValueTree> added;

    for (auto& cn : notes)
    {
        const double s = at + cn.start;
        if (s >= len - 1e-6) continue;
        const double l = juce::jmin (cn.length, len - s);
        if (auto* n = c->getSequence().addNote (cn.pitch, te::BeatPosition::fromBeats (s),
                                                te::BeatDuration::fromBeats (l), cn.velocity, 0, um()))
            added.add (n->state);
    }
    setSelection (added);
}

void NoteGrid::selectAll()
{
    juce::Array<juce::ValueTree> s;
    if (auto* c = clip())
        for (auto* n : c->getSequence().getNotes())
            s.add (n->state);
    setSelection (s);
}

void NoteGrid::deselectAll() { setSelection ({}); }

void NoteGrid::deleteSelection()
{
    auto* c = clip();
    if (c == nullptr) return;
    edit->getUndoManager().beginNewTransaction();
    for (auto& vt : selection)
        if (auto* n = noteFor (vt))
            c->getSequence().removeNote (*n, um());
    setSelection ({});
}

void NoteGrid::copySelection()
{
    pruneSelection();
    if (selection.isEmpty()) return;
    double base = 0, span = 0;
    noteClipboard() = collect (true, base, span);
    noteClipboardSpan() = span;
    pasteBeat = base + span;
    repaint();
}

void NoteGrid::cutSelection()
{
    pruneSelection();
    if (selection.isEmpty()) return;
    double base = 0, span = 0;
    noteClipboard() = collect (true, base, span);
    noteClipboardSpan() = span;
    deleteSelection();
    pasteBeat = base;
}

void NoteGrid::paste()
{
    if (noteClipboard().empty() || clip() == nullptr) return;
    edit->getUndoManager().beginNewTransaction();
    pasteNotes (noteClipboard(), pasteBeat, noteClipboardSpan());
    pasteBeat += noteClipboardSpan();
    repaint();
}

void NoteGrid::duplicateSelection()
{
    pruneSelection();
    if (selection.isEmpty()) return;
    double base = 0, span = 0;
    auto notes = collect (true, base, span);
    edit->getUndoManager().beginNewTransaction();
    pasteNotes (notes, base + span, span);
    pasteBeat = base + 2.0 * span;
}

void NoteGrid::quantize()
{
    auto* c = clip();
    if (c == nullptr) return;
    pruneSelection();
    const bool all = selection.isEmpty();
    const double len = clipLengthBeats();
    edit->getUndoManager().beginNewTransaction();

    for (auto* n : c->getSequence().getNotes())
        if (all || isSelected (n))
        {
            const double s = juce::jmin (len - snap, std::round (n->getStartBeat().inBeats() / snap) * snap);
            n->setStartAndLength (te::BeatPosition::fromBeats (juce::jmax (0.0, s)), n->getLengthBeats(), um());
        }
    repaint();
}

void NoteGrid::transpose (int semis)
{
    pruneSelection();
    if (selection.isEmpty()) return;
    edit->getUndoManager().beginNewTransaction();
    for (auto& vt : selection)
        if (auto* n = noteFor (vt))
            n->setNoteNumber (juce::jlimit (0, 127, n->getNoteNumber() + semis), um());
    repaint();
}

void NoteGrid::nudge (int steps)
{
    pruneSelection();
    if (selection.isEmpty()) return;
    const double len = clipLengthBeats();
    edit->getUndoManager().beginNewTransaction();
    for (auto& vt : selection)
        if (auto* n = noteFor (vt))
        {
            const double s = juce::jlimit (0.0, juce::jmax (0.0, len - snap), n->getStartBeat().inBeats() + steps * snap);
            n->setStartAndLength (te::BeatPosition::fromBeats (s), n->getLengthBeats(), um());
        }
    repaint();
}

//==============================================================================
// StepGrid
//==============================================================================
StepGrid::StepGrid()
{
    lanes = { { "Kick",        36 }, { "Rim shot",   37 }, { "Snare",     38 }, { "Clap",      39 },
              { "Closed hat",  42 }, { "Open hat",   46 }, { "Low tom",   45 }, { "Hi tom",    50 },
              { "Cowbell",     56 }, { "Cymbal",     49 } };
}

int StepGrid::numBars() const
{
    return juce::jmax (1, (int) std::ceil (clipBeats (edit, clip()) / 4.0 - 1e-6));
}

float StepGrid::rowH() const { return juce::jmax (12.0f, (float) getHeight() / (float) lanes.size()); }

juce::Rectangle<float> StepGrid::cellBounds (int row, int step) const
{
    const float cw = (getWidth() - labelWidth) / 16.0f;
    return { labelWidth + step * cw + 2.0f, row * rowH() + 2.0f, cw - 4.0f, rowH() - 4.0f };
}

bool StepGrid::cellAt (juce::Point<int> p, int& row, int& step) const
{
    row = juce::jlimit (0, (int) lanes.size() - 1, (int) (p.y / rowH()));
    step = juce::jlimit (0, 15, (int) ((p.x - labelWidth) / ((getWidth() - labelWidth) / 16.0f)));
    return p.x >= labelWidth && p.y >= 0 && p.y < getHeight();
}

te::MidiNote* StepGrid::findStep (int row, int step) const
{
    auto* c = clip();
    if (c == nullptr) return nullptr;

    const double b0 = stepBeat (step);
    for (auto* n : c->getSequence().getNotes())
    {
        const double s = n->getStartBeat().inBeats();
        if (n->getNoteNumber() == lanes[(size_t) row].note && s >= b0 - 1e-4 && s < b0 + 0.25 - 1e-4)
            return n;
    }
    return nullptr;
}

void StepGrid::setStep (int row, int step, bool on, bool accent)
{
    auto* c = clip();
    if (c == nullptr) return;

    const double beat = stepBeat (step);
    if (beat >= clipBeats (edit, c)) return;

    auto* um = &edit->getUndoManager();
    auto* existing = findStep (row, step);

    if (on && existing == nullptr)
        c->getSequence().addNote (lanes[(size_t) row].note, te::BeatPosition::fromBeats (beat),
                                  te::BeatDuration::fromBeats (0.25), accent ? 127 : 96, 0, um);
    else if (on && existing != nullptr)
        existing->setVelocity (accent ? 127 : 96, um);
    else if (! on && existing != nullptr)
        c->getSequence().removeNote (*existing, um);
}

void StepGrid::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    auto* c = clip();
    const double len = clipBeats (edit, c);
    const double ph = playheadInClip (edit, c);
    const int playStep = (ph >= page * 4.0 && ph < page * 4.0 + 4.0) ? (int) ((ph - page * 4.0) / 0.25) : -1;

    g.setFont (juce::Font (juce::FontOptions (13.0f)));

    for (int r = 0; r < (int) lanes.size(); ++r)
    {
        const float y = r * rowH();
        g.setColour (colours::panel);
        g.fillRect (0.0f, y + 1.0f, (float) labelWidth - 4.0f, rowH() - 2.0f);
        g.setColour (colours::text);
        g.drawText (lanes[(size_t) r].name, 8, (int) y, labelWidth - 50, (int) rowH(), juce::Justification::centredLeft);
        g.setColour (colours::dimText);
        g.drawText (juce::String (lanes[(size_t) r].note), labelWidth - 44, (int) y, 36, (int) rowH(), juce::Justification::centredRight);

        for (int s = 0; s < 16; ++s)
        {
            auto cell = cellBounds (r, s);
            const bool valid = stepBeat (s) < len;
            auto* n = valid ? findStep (r, s) : nullptr;

            juce::Colour base = (s / 4) % 2 == 0 ? colours::panelLight : colours::panel;
            if (! valid) base = colours::background.brighter (0.02f);

            g.setColour (n != nullptr ? (n->getVelocity() >= 115 ? colours::accent.brighter (0.35f) : colours::accent) : base);
            g.fillRoundedRectangle (cell, 3.0f);

            if (s == playStep)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.drawRoundedRectangle (cell, 3.0f, 1.5f);
            }
        }
    }

    if (hasSel)
    {
        auto a = cellBounds (selRow0, selStep0), b = cellBounds (selRow1, selStep1);
        auto sel = a.getUnion (b).expanded (2.0f);
        g.setColour (colours::selection.withAlpha (0.15f));
        g.fillRect (sel);
        g.setColour (colours::selection);
        g.drawRect (sel, 1.5f);
    }

    {
        const float x = cellBounds (0, juce::jlimit (0, 15, cursorStep)).getX() - 2.0f;
        g.setColour (colours::selection.withAlpha (0.7f));
        g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
    }

    if (c == nullptr)
    {
        g.setColour (colours::dimText);
        g.drawText ("Double-click a clip on the drum track to program it here.",
                    getLocalBounds().withTrimmedLeft (labelWidth), juce::Justification::centred);
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    lastRow = lastStep = -1;
    selecting = false;
    if (clip() == nullptr) return;

    if (e.x < labelWidth)
    {
        const int row = (int) (e.y / rowH());
        if (juce::isPositiveAndBelow (row, (int) lanes.size()))
            showLaneMenu (row);
        return;
    }

    int r = 0, s = 0;
    if (! cellAt (e.getPosition(), r, s)) return;

    edit->getUndoManager().beginNewTransaction();
    cursorStep = s;

    if (tool == EditTool::select && ! e.mods.isPopupMenu())
    {
        selecting = true;
        anchorRow = r; anchorStep = s;
        selRow0 = selRow1 = r; selStep0 = selStep1 = s;
        hasSel = true;
        repaint();
        return;
    }

    hasSel = false;

    if (tool == EditTool::erase || e.mods.isPopupMenu())
    {
        paintAdds = false;
        setStep (r, s, false, false);
    }
    else
    {
        auto* existing = findStep (r, s);
        const bool accent = e.mods.isShiftDown();

        if (accent && existing != nullptr)
        {
            setStep (r, s, true, existing->getVelocity() < 115);
            paintAdds = true;
        }
        else
        {
            paintAdds = existing == nullptr;
            setStep (r, s, paintAdds, accent);
        }
    }

    lastRow = r; lastStep = s;
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    int r = 0, s = 0;
    if (clip() == nullptr || ! cellAt (e.getPosition(), r, s)) return;

    if (selecting)
    {
        selRow0 = juce::jmin (anchorRow, r);  selRow1 = juce::jmax (anchorRow, r);
        selStep0 = juce::jmin (anchorStep, s); selStep1 = juce::jmax (anchorStep, s);
        cursorStep = selStep0;
        repaint();
        return;
    }

    if (r == lastRow && s == lastStep) return;

    setStep (r, s, paintAdds, e.mods.isShiftDown());
    lastRow = r; lastStep = s;
    repaint();
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    selecting = false;
}

void StepGrid::selectAll()
{
    hasSel = true;
    selRow0 = 0; selRow1 = (int) lanes.size() - 1;
    selStep0 = 0; selStep1 = 15;
    cursorStep = 0;
    repaint();
}

void StepGrid::deselectAll() { hasSel = false; repaint(); }

void StepGrid::deleteSelection()
{
    if (! hasSel || clip() == nullptr) return;
    edit->getUndoManager().beginNewTransaction();
    for (int r = selRow0; r <= selRow1; ++r)
        for (int s = selStep0; s <= selStep1; ++s)
            setStep (r, s, false, false);
    repaint();
}

void StepGrid::copySelection()
{
    auto* c = clip();
    if (! hasSel || c == nullptr) return;

    const double from = stepBeat (selStep0);
    const double to   = stepBeat (selStep1) + 0.25;
    std::vector<ClipNote> out;

    for (auto* n : c->getSequence().getNotes())
    {
        const double s = n->getStartBeat().inBeats();
        if (s < from - 1e-4 || s >= to - 1e-4) continue;

        bool inRows = false;
        for (int r = selRow0; r <= selRow1; ++r)
            if (lanes[(size_t) r].note == n->getNoteNumber()) inRows = true;
        if (! inRows) continue;

        out.push_back ({ n->getNoteNumber(), s - from, n->getLengthBeats().inBeats(), n->getVelocity() });
    }

    noteClipboard() = out;
    noteClipboardSpan() = to - from;
    cursorStep = selStep1 + 1;
    if (cursorStep > 15 && page < numBars() - 1) { cursorStep = 0; ++page; if (onPageChanged) onPageChanged(); }
    cursorStep = juce::jmin (cursorStep, 15);
    repaint();
}

void StepGrid::cutSelection()
{
    if (! hasSel) return;
    const int keep = selStep0;
    copySelection();
    deleteSelection();
    cursorStep = keep;
}

void StepGrid::paste()
{
    auto* c = clip();
    if (c == nullptr || noteClipboard().empty()) return;

    edit->getUndoManager().beginNewTransaction();
    const double at  = stepBeat (cursorStep);
    const double len = clipBeats (edit, c);
    auto* um = &edit->getUndoManager();

    for (auto& cn : noteClipboard())
    {
        const double s = at + cn.start;
        if (s >= len - 1e-6) continue;

        for (auto* n : c->getSequence().getNotes())
            if (n->getNoteNumber() == cn.pitch && std::abs (n->getStartBeat().inBeats() - s) < 1e-3)
            { c->getSequence().removeNote (*n, um); break; }

        c->getSequence().addNote (cn.pitch, te::BeatPosition::fromBeats (s),
                                  te::BeatDuration::fromBeats (juce::jmin (cn.length, len - s)), cn.velocity, 0, um);
    }

    const int stepsPasted = juce::jmax (1, (int) std::round (noteClipboardSpan() / 0.25));
    int next = cursorStep + stepsPasted;
    while (next > 15 && page < numBars() - 1) { next -= 16; ++page; }
    cursorStep = juce::jlimit (0, 15, next);
    hasSel = false;
    if (onPageChanged) onPageChanged();
    repaint();
}

void StepGrid::duplicateSelection()
{
    if (! hasSel) return;
    copySelection();
    paste();
}

void StepGrid::fillRow (int row, int every)
{
    if (clip() == nullptr) return;
    edit->getUndoManager().beginNewTransaction();
    for (int s = 0; s < 16; ++s)
        setStep (row, s, s % every == 0, false);
    repaint();
}

void StepGrid::clearRow (int row)
{
    if (clip() == nullptr) return;
    edit->getUndoManager().beginNewTransaction();
    for (int s = 0; s < 16; ++s)
        setStep (row, s, false, false);
    repaint();
}

void StepGrid::showLaneMenu (int row)
{
    juce::PopupMenu notes;
    const int current = lanes[(size_t) row].note;
    for (int n = 24; n <= 96; ++n)
        notes.addItem (n + 1, juce::String (n) + "   " + juce::MidiMessage::getMidiNoteName (n, true, true, 3),
                       true, n == current);

    juce::PopupMenu m;
    m.addSectionHeader (lanes[(size_t) row].name + " (this bar)");
    m.addItem (201, "Fill every step");
    m.addItem (202, "Fill every 2nd step");
    m.addItem (204, "Fill every 4th step");
    m.addItem (208, "Fill every 8th step");
    m.addItem (300, "Clear row");
    m.addSeparator();
    m.addSubMenu ("Change MIDI note", notes);

    m.showMenuAsync (juce::PopupMenu::Options(),
                     [safe = juce::Component::SafePointer<StepGrid> (this), row] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (r >= 1 && r <= 128)      { safe->lanes[(size_t) row].note = r - 1; safe->repaint(); }
        else if (r > 200 && r < 300) safe->fillRow (row, r - 200);
        else if (r == 300)           safe->clearRow (row);
    });
}

//==============================================================================
// MidiEditorPanel
//==============================================================================
MidiEditorPanel::MidiEditorPanel()
{
    auto style = [this] (juce::TextButton& b, const juce::String& tip)
    {
        b.setColour (juce::TextButton::buttonColourId, colours::panelLight);
        b.setColour (juce::TextButton::buttonOnColourId, colours::accent);
        b.setWantsKeyboardFocus (false);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    };

    style (pianoTab, "Piano roll editor");
    style (stepTab, "Drum step editor (16 steps per bar)");
    style (prevBar, "Previous bar");
    style (nextBar, "Next bar");
    style (closeButton, "Close editor (Ctrl+E)");

    style (selectTool, "Select tool (1): drag a box to select, drag notes to move, Shift = add to selection");
    style (drawTool,   "Draw tool (2): click to add, drag to set length, Shift-drag = velocity");
    style (eraseTool,  "Erase tool (3): click or drag over notes to delete");
    style (sliceTool,  "Slice tool (4): click a note to cut it in two at the grid");

    style (cutButton,       "Cut (Ctrl+X)");
    style (copyButton,      "Copy (Ctrl+C)");
    style (pasteButton,     "Paste at the yellow marker (Ctrl+V)");
    style (deleteButton,    "Delete selection (Del)");
    style (dupButton,       "Duplicate selection right after itself (Ctrl+D)");
    style (quantizeButton,  "Quantize selection (or all notes) to the grid (Q)");
    style (selectAllButton, "Select all (Ctrl+A)");

    pianoTab.onClick    = [this] { setMode (false); };
    stepTab.onClick     = [this] { setMode (true); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    prevBar.onClick     = [this] { stepGrid.page = juce::jmax (0, stepGrid.page - 1); updateLabels(); stepGrid.repaint(); };
    nextBar.onClick     = [this] { stepGrid.page = juce::jmin (stepGrid.numBars() - 1, stepGrid.page + 1); updateLabels(); stepGrid.repaint(); };

    selectTool.onClick = [this] { setTool (EditTool::select); };
    drawTool.onClick   = [this] { setTool (EditTool::draw); };
    eraseTool.onClick  = [this] { setTool (EditTool::erase); };
    sliceTool.onClick  = [this] { setTool (EditTool::slice); };

    cutButton.onClick       = [this] { if (stepMode) stepGrid.cutSelection();       else noteGrid.cutSelection(); };
    copyButton.onClick      = [this] { if (stepMode) stepGrid.copySelection();      else noteGrid.copySelection(); };
    pasteButton.onClick     = [this] { if (stepMode) stepGrid.paste();              else noteGrid.paste(); };
    deleteButton.onClick    = [this] { if (stepMode) stepGrid.deleteSelection();    else noteGrid.deleteSelection(); };
    dupButton.onClick       = [this] { if (stepMode) stepGrid.duplicateSelection(); else noteGrid.duplicateSelection(); };
    quantizeButton.onClick  = [this] { noteGrid.quantize(); };
    selectAllButton.onClick = [this] { if (stepMode) stepGrid.selectAll();          else noteGrid.selectAll(); };

    stepGrid.onPageChanged = [this] { updateLabels(); };

    snapBox.addItem ("Grid 1/4",  4);
    snapBox.addItem ("Grid 1/8",  8);
    snapBox.addItem ("Grid 1/16", 16);
    snapBox.addItem ("Grid 1/32", 32);
    snapBox.setSelectedId (16, juce::dontSendNotification);
    snapBox.onChange = [this] { noteGrid.snap = 4.0 / snapBox.getSelectedId(); noteGrid.repaint(); };
    snapBox.setWantsKeyboardFocus (false);
    addAndMakeVisible (snapBox);

    clipLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    clipLabel.setColour (juce::Label::textColourId, colours::accent);
    barLabel.setColour (juce::Label::textColourId, colours::text);
    barLabel.setJustificationType (juce::Justification::centred);
    hintLabel.setColour (juce::Label::textColourId, colours::dimText);
    hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    for (auto* l : { &clipLabel, &barLabel, &hintLabel })
    {
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    }

    pianoView.setViewedComponent (&noteGrid, false);
    pianoView.setScrollBarsShown (true, false);
    addAndMakeVisible (pianoView);
    addChildComponent (stepGrid);

    addMouseListener (this, true);

    setTool (EditTool::draw);
    setMode (false);
}

MidiEditorPanel::~MidiEditorPanel()
{
    removeMouseListener (this);
}

void MidiEditorPanel::mouseDown (const juce::MouseEvent&)
{
    wantsKeys = true;
}

void MidiEditorPanel::setClip (te::Edit* e, te::EditItemID id, bool step)
{
    const bool changed = ! (id == clipID) || e != edit;
    edit = e;
    clipID = id;
    noteGrid.edit = stepGrid.edit = e;
    noteGrid.clipID = stepGrid.clipID = id;

    if (changed)
    {
        noteGrid.clipChanged();
        stepGrid.page = 0;
        stepGrid.deselectAll();

        int centre = 66;
        if (auto* c = findMidiClip (e, id))
        {
            auto& notes = c->getSequence().getNotes();
            if (notes.size() > 0)
            {
                int sum = 0;
                for (auto* n : notes) sum += n->getNoteNumber();
                centre = sum / notes.size();
            }
        }
        resized();
        pianoView.setViewPosition (0, juce::jmax (0, (int) noteGrid.noteToY (centre) - pianoView.getHeight() / 2));
    }

    setMode (step);
}

void MidiEditorPanel::setTool (EditTool t)
{
    if (stepMode && t == EditTool::slice) t = EditTool::draw;
    noteGrid.tool = stepGrid.tool = t;
    selectTool.setToggleState (t == EditTool::select, juce::dontSendNotification);
    drawTool.setToggleState   (t == EditTool::draw,   juce::dontSendNotification);
    eraseTool.setToggleState  (t == EditTool::erase,  juce::dontSendNotification);
    sliceTool.setToggleState  (t == EditTool::slice,  juce::dontSendNotification);
    updateLabels();
}

void MidiEditorPanel::setMode (bool step)
{
    stepMode = step;
    pianoTab.setToggleState (! step, juce::dontSendNotification);
    stepTab.setToggleState (step, juce::dontSendNotification);
    pianoView.setVisible (! step);
    stepGrid.setVisible (step);
    snapBox.setVisible (! step);
    sliceTool.setVisible (! step);
    quantizeButton.setVisible (! step);
    prevBar.setVisible (step);
    nextBar.setVisible (step);
    barLabel.setVisible (step);
    if (step && noteGrid.tool == EditTool::slice) setTool (EditTool::draw);
    updateLabels();
    resized();
}

void MidiEditorPanel::updateLabels()
{
    auto* c = findMidiClip (edit, clipID);
    clipLabel.setText (c != nullptr ? c->getName() + "  (" + c->getTrack()->getName() + ")" : "No clip selected",
                       juce::dontSendNotification);

    stepGrid.page = juce::jlimit (0, stepGrid.numBars() - 1, stepGrid.page);
    barLabel.setText ("Bar " + juce::String (stepGrid.page + 1) + " / " + juce::String (stepGrid.numBars()),
                      juce::dontSendNotification);

    juce::String hint;
    switch (noteGrid.tool)
    {
        case EditTool::select: hint = stepMode ? "Drag over steps to select. Ctrl+C / Ctrl+V copies to the yellow marker."
                                               : "Drag a box to select. Drag notes to move, edge = resize, Ctrl-drag = velocity, arrows = nudge/transpose.";
                               break;
        case EditTool::draw:   hint = stepMode ? "Click/drag to set steps. Shift = accent. Right-click a drum name for fill / note."
                                               : "Click = add note, drag = length. Shift-drag = velocity. Right-click = delete. Alt = no snap.";
                               break;
        case EditTool::erase:  hint = "Click or drag over notes to delete them."; break;
        case EditTool::slice:  hint = "Click a note to cut it in two at the grid (Alt = free)."; break;
    }
    hintLabel.setText (hint, juce::dontSendNotification);
}

bool MidiEditorPanel::handleKey (const juce::KeyPress& k)
{
    if (! isVisible() || findMidiClip (edit, clipID) == nullptr) return false;

    const auto cmd = juce::ModifierKeys::commandModifier;
    const juce::juce_wchar ch = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (k.getTextCharacter());

    if (k == juce::KeyPress ('c', cmd, 0)) { copyButton.triggerClick();      return true; }
    if (k == juce::KeyPress ('x', cmd, 0)) { cutButton.triggerClick();       return true; }
    if (k == juce::KeyPress ('v', cmd, 0)) { pasteButton.triggerClick();     return true; }
    if (k == juce::KeyPress ('d', cmd, 0)) { dupButton.triggerClick();       return true; }
    if (k == juce::KeyPress ('a', cmd, 0)) { selectAllButton.triggerClick(); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteButton.triggerClick(); return true; }
    if (k == juce::KeyPress::escapeKey) { noteGrid.deselectAll(); stepGrid.deselectAll(); return true; }

    if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;

    if (ch == '1') { setTool (EditTool::select); return true; }
    if (ch == '2') { setTool (EditTool::draw);   return true; }
    if (ch == '3') { setTool (EditTool::erase);  return true; }
    if (ch == '4' && ! stepMode) { setTool (EditTool::slice); return true; }
    if (ch == 'q' && ! stepMode) { noteGrid.quantize(); return true; }

    if (! stepMode && noteGrid.hasSelection())
    {
        const bool shift = k.getModifiers().isShiftDown();
        if (k.getKeyCode() == juce::KeyPress::upKey)    { noteGrid.transpose (shift ? 12 : 1);   return true; }
        if (k.getKeyCode() == juce::KeyPress::downKey)  { noteGrid.transpose (shift ? -12 : -1); return true; }
        if (k.getKeyCode() == juce::KeyPress::leftKey)  { noteGrid.nudge (-1); return true; }
        if (k.getKeyCode() == juce::KeyPress::rightKey) { noteGrid.nudge (1);  return true; }
    }
    return false;
}

void MidiEditorPanel::tick()
{
    if (! isVisible()) return;
    updateLabels();
    if (stepMode) stepGrid.repaint();
    else          noteGrid.repaint();
}

void MidiEditorPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (wantsKeys ? colours::accent : colours::accent.withAlpha (0.4f));
    g.fillRect (0, 0, getWidth(), 2);
}

void MidiEditorPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (2);

    auto row1 = r.removeFromTop (32).reduced (6, 3);
    pianoTab.setBounds (row1.removeFromLeft (96));
    row1.removeFromLeft (4);
    stepTab.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (12);
    closeButton.setBounds (row1.removeFromRight (64));
    row1.removeFromRight (10);
    if (stepMode)
    {
        nextBar.setBounds (row1.removeFromRight (30));
        barLabel.setBounds (row1.removeFromRight (90));
        prevBar.setBounds (row1.removeFromRight (30));
    }
    else
    {
        snapBox.setBounds (row1.removeFromRight (110));
    }
    row1.removeFromRight (10);
    clipLabel.setBounds (row1);

    auto row2 = r.removeFromTop (30).reduced (6, 2);
    for (auto* b : { &selectTool, &drawTool, &eraseTool })
        b->setBounds (row2.removeFromLeft (62).reduced (1, 0));
    if (! stepMode) sliceTool.setBounds (row2.removeFromLeft (62).reduced (1, 0));
    row2.removeFromLeft (14);
    for (auto* b : { &cutButton, &copyButton, &pasteButton, &deleteButton, &selectAllButton })
        b->setBounds (row2.removeFromLeft (58).reduced (1, 0));
    dupButton.setBounds (row2.removeFromLeft (80).reduced (1, 0));
    if (! stepMode) quantizeButton.setBounds (row2.removeFromLeft (76).reduced (1, 0));
    row2.removeFromLeft (12);
    hintLabel.setBounds (row2);

    pianoView.setBounds (r);
    noteGrid.setSize (juce::jmax (200, pianoView.getMaximumVisibleWidth()), (int) (128 * NoteGrid::noteHeight));
    stepGrid.setBounds (r.reduced (6, 4));
}
