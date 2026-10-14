#include "InstrumentPanel.h"
#include "../PluginProcessor.h"
#include "../dsp/AcidEngine.h"
#include "../dsp/ArpEngine.h"
#include "../dsp/CompEngine.h"
#include "../dsp/JunoEngine.h"
#include "../dsp/StringEngine.h"
#include "../dsp/ThereminEngine.h"
#include "../music/JunoChords.h"
#include "../music/Scales.h"

#include <cmath>
#include <cstring>

using namespace juce;

namespace chordtroller
{

namespace
{
    const Colour ground { 0xff17120f };
    const Colour panel  { 0xff1e1815 };
    const Colour ink    { 0xfff2ede9 };
    const Colour muted  { 0xffa49c96 };
    const Colour hair   { 0xff332b27 };
    const Colour edge   { 0xff4a413c };
    const Colour accent { 0xffff5c3d };

    Font mono (float size, bool bold = false)
    {
        return Font (FontOptions (Font::getDefaultMonospacedFontName(), size,
                                  bold ? Font::bold : Font::plain));
    }

    void panelBox (Graphics& g, Rectangle<int> area, const String& title)
    {
        g.setColour (panel);
        g.fillRect (area);
        g.setColour (hair);
        g.drawRect (area, 1);

        g.setColour (muted);
        g.setFont (mono (10.0f, true));
        g.drawText (title, area.reduced (12, 8).removeFromTop (13),
                    Justification::centredLeft);
    }

    String noteName (int midiNote)
    {
        if (midiNote < 0)
            return "-";

        return String (music::pitchClassName (midiNote % 12)) + String (midiNote / 12 - 1);
    }

    /** A single note spelt the way its scale wants it. */
    String noteNameIn (int midiNote, int keyRoot, int scaleIndex)
    {
        if (midiNote < 0)
            return "-";

        const bool sharps = music::spellWithSharps (keyRoot, music::scaleAt (scaleIndex).mask);
        return String (music::pitchClassNameSpelt (midiNote % 12, sharps)) + String (midiNote / 12 - 1);
    }

    /** Where every region sits. Computed once and used by both paint() and
        resized(), because a checkbox positioned by one set of numbers and drawn
        against another drifts apart the first time a margin changes. */
    struct PanelLayout
    {
        Rectangle<int> header, ring, plate, triggers, readout;
    };

    PanelLayout layoutOf (Rectangle<int> bounds)
    {
        PanelLayout l;
        auto area = bounds.reduced (20);

        l.header = area.removeFromTop (54);
        area.removeFromTop (14);

        // Claimed from the bottom before the middle gets what is left, so
        // nothing can be pushed off the edge by a window that is a little short.
        l.readout = area.removeFromBottom (82);
        area.removeFromBottom (14);

        l.triggers = area.removeFromBottom (118);
        area.removeFromBottom (14);

        l.ring = area.removeFromLeft (jlimit (260, 400, area.getWidth() * 2 / 5));
        area.removeFromLeft (14);
        l.plate = area;

        return l;
    }

    /** The plate panel's bottom row, which the strum reading shares with the
        vibration tick. */
    Rectangle<int> plateFootRow (Rectangle<int> plate)
    {
        return plate.reduced (16).removeFromBottom (26);
    }

    /** One cell of the readout strip. Shared by the drawing and by the tempo
        drag, so the number you grab is the number you see. */
    Rectangle<int> readoutCell (Rectangle<int> strip, int index, int count)
    {
        const int width = strip.getWidth() / count;
        return strip.withX (strip.getX() + index * width).withWidth (width);
    }

    constexpr int kBarLabel   = 40;
    constexpr int kBarReading = 110;
    constexpr int kBarRow     = 22;
    constexpr int kBarGap     = 4;

    Rectangle<int> levelRow (Rectangle<int> levels, int index)
    {
        auto body = levels.reduced (16, 12).withTrimmedTop (14);
        body.removeFromTop (index * (kBarRow + kBarGap));
        return body.removeFromTop (kBarRow);
    }

    /** A level bar's reading: its fader position in decibels. */
    String levelReading (float position)
    {
        if (position <= 0.0f)
            return "tyst";

        return String (pad::faderDecibels (position), 1) + " dB";
    }

    Rectangle<int> barTrack (Rectangle<int> row)
    {
        row.removeFromLeft (kBarLabel);
        row.removeFromRight (kBarReading);
        return row.reduced (6, 7);
    }

    class PanelLook final : public juce::LookAndFeel_V4
    {
    public:
        void drawToggleButton (Graphics& g, ToggleButton& button, bool highlighted, bool) override
        {
            auto area = button.getLocalBounds();
            auto box = area.removeFromLeft (16).withSizeKeepingCentre (13, 13);
            const bool on = button.getToggleState();

            g.setColour (on ? accent : (highlighted ? muted : edge));
            g.drawRect (box, on ? 2 : 1);

            if (on)
            {
                g.setColour (accent);
                g.fillRect (box.reduced (4));
            }

            g.setColour (on ? ink : muted);
            g.setFont (mono (10.5f, true));
            g.drawText (button.getButtonText(), area.withTrimmedLeft (7),
                        Justification::centredLeft);
        }
    };
}

//==============================================================================

InstrumentPanel::InstrumentPanel (ChordtrollerAudioProcessor& p)
    : processor (p)
{
    look = std::make_unique<PanelLook>();
    rumble.setLookAndFeel (look.get());

    rumble.setToggleState (processor.rumbleEnabled(), dontSendNotification);
    rumble.onClick = [this] { processor.setRumbleEnabled (rumble.getToggleState()); };
    addAndMakeVisible (rumble);

    tempoField.setFont (mono (20.0f, true));
    tempoField.setJustificationType (Justification::centred);
    tempoField.setColour (Label::textColourId, ink);
    tempoField.setColour (Label::backgroundColourId, Colours::transparentBlack);
    tempoField.setColour (Label::backgroundWhenEditingColourId, ground);
    tempoField.setColour (Label::textWhenEditingColourId, accent);
    tempoField.setColour (Label::outlineWhenEditingColourId, accent);
    tempoField.setEditable (true, true, false);      // one click is enough

    tempoField.onTextChange = [this]
    {
        const auto typed = tempoField.getText().getIntValue();

        // A blank or nonsense entry keeps what was there rather than snapping to
        // forty; the timer will put the old number back on the next frame.
        if (typed >= 40 && typed <= 220)
            processor.setInternalTempo ((double) typed);
    };

    addAndMakeVisible (tempoField);

    // Ids are the Instrument enum plus one — ComboBox reserves zero for "none".
    instrumentBox.addItem ("CHORDTROLLER", 1);
    instrumentBox.addItem ("THEREMIN", 2);
    instrumentBox.addItem ("JUNO", 3);
    instrumentBox.addItem ("303", 4);
    instrumentBox.addItem ("STRAKAR", 5);
    instrumentBox.addItem ("ARP", 6);
    instrumentBox.setSelectedId ((int) processor.instrument() + 1, dontSendNotification);
    instrumentBox.setColour (ComboBox::backgroundColourId, panel);
    instrumentBox.setColour (ComboBox::textColourId, ink);
    instrumentBox.setColour (ComboBox::outlineColourId, edge);
    instrumentBox.setColour (ComboBox::arrowColourId, accent);
    instrumentBox.onChange = [this]
    {
        processor.setInstrument ((ChordtrollerAudioProcessor::Instrument)
                                   jlimit (0, 5, instrumentBox.getSelectedId() - 1));
        resized();
        repaint();
    };
    addAndMakeVisible (instrumentBox);

    midiPortBox.setColour (ComboBox::backgroundColourId, panel);
    midiPortBox.setColour (ComboBox::textColourId, ink);
    midiPortBox.setColour (ComboBox::outlineColourId, edge);
    midiPortBox.setColour (ComboBox::arrowColourId, accent);
    midiPortBox.onChange = [this]
    {
        const int index = midiPortBox.getSelectedId() - 2;
        processor.setMidiPort (isPositiveAndBelow (index, midiPortIds.size()) ? midiPortIds[index]
                                                                              : String());
    };
    refreshMidiPorts();
    addAndMakeVisible (midiPortBox);

    startTimerHz (60);
}

InstrumentPanel::~InstrumentPanel()
{
    // The look-and-feel is about to be destroyed and the button still points at
    // it, so it has to be let go of first.
    rumble.setLookAndFeel (nullptr);
}

String InstrumentPanel::buttonName (const char* xboxName) const
{
    // Positions are the same on every layout and are what the code reads, so
    // only the printed names change. A Steam Controller keeps Xbox letters but
    // has trackpads where the d-pad and the right stick would be.
    static const std::pair<const char*, const char*> nintendo[]
    {
        { "A", "B" }, { "B", "A" }, { "X", "Y" }, { "Y", "X" },
        { "LB", "L" }, { "RB", "R" }, { "LT", "ZL" }, { "RT", "ZR" },
        { "VIEW", "-" }, { "MENY", "+" }
    };

    static const std::pair<const char*, const char*> steam[]
    {
        { "D-PAD", "V PLATTA" }, { "H SPAK", "H PLATTA" }, { "HOGER SPAK", "HOGER PLATTA" },
        { "VIEW", "BACK" }, { "MENY", "START" }
    };

    auto lookUp = [xboxName] (const auto& table) -> const char*
    {
        for (const auto& n : table)
            if (std::strcmp (n.first, xboxName) == 0)
                return n.second;

        return xboxName;
    };

    switch (pad.layout)
    {
        case PadState::Layout::nintendo: return lookUp (nintendo);
        case PadState::Layout::steam:    return lookUp (steam);
        case PadState::Layout::xbox:     break;
    }

    return xboxName;
}

bool InstrumentPanel::isTheremin() const
{
    return processor.instrument() == ChordtrollerAudioProcessor::Instrument::theremin;
}

void InstrumentPanel::resized()
{
    const auto l = layoutOf (getLocalBounds());

    auto header = l.header;
    header.removeFromLeft (header.getWidth() / 2);
    instrumentBox.setBounds (header.removeFromLeft (170).withSizeKeepingCentre (170, 26));
    header.removeFromLeft (8);
    midiPortBox.setBounds (header.removeFromLeft (170).withSizeKeepingCentre (170, 26));

    auto foot = plateFootRow (l.plate);
    rumble.setBounds (foot.removeFromRight (128).withSizeKeepingCentre (128, 22));

    // The same inner rectangle drawReadout uses, minus the caption row, so the
    // field sits exactly where the number was drawn.
    auto inner = readoutCell (l.readout, 4, 5).reduced (8, 14);
    inner.removeFromTop (14);
    tempoField.setBounds (inner);

    // Only the strummed strings knock the hand. The theremin and the bowed
    // strings have no tempo of their own; the Juno's phrases do, so it keeps
    // the tempo cell.
    rumble.setVisible (! isTheremin() && ! isJuno() && ! isAcid() && ! isStrings() && ! isArp());
    tempoField.setVisible (! isTheremin() && ! isStrings());
}

bool InstrumentPanel::isJuno() const
{
    return processor.instrument() == ChordtrollerAudioProcessor::Instrument::juno;
}

void InstrumentPanel::timerCallback()
{
    pad = processor.gamepad().state();

    // A saved song can change the instrument before or after the panel opens.
    const int shownInstrument = (int) processor.instrument() + 1;

    if (instrumentBox.getSelectedId() != shownInstrument)
    {
        instrumentBox.setSelectedId (shownInstrument, dontSendNotification);
        resized();
    }

    if (--midiRefreshCountdown <= 0)
    {
        midiRefreshCountdown = 120;
        refreshMidiPorts();
    }

    const auto& published = processor.published();
    ring.setKeyRoot (published.keyRoot.load (std::memory_order_relaxed));
    // The Juno always reads the seventh bank and colours it by genre; the
    // strings read whichever bank LB has them on.
    ring.setBank (isJuno() ? 1 : published.bank.load (std::memory_order_relaxed));
    ring.setMode (published.minorMode.load (std::memory_order_relaxed)
                    ? music::Mode::minor : music::Mode::major);

    // The host owns the tempo whenever it is telling us one, so the field goes
    // read-only rather than pretending to accept a number it will lose.
    const bool fromHost = published.tempoFromHost.load (std::memory_order_relaxed);

    if (tempoField.isEditable() == fromHost)
    {
        tempoField.setEditable (! fromHost, ! fromHost, false);
        tempoField.setColour (Label::textColourId, fromHost ? muted : ink);
    }

    if (! tempoField.isBeingEdited())
    {
        const auto bpm = String ((int) std::lround (
            published.tempo.load (std::memory_order_relaxed)));

        if (tempoField.getText() != bpm)
            tempoField.setText (bpm, dontSendNotification);
    }

    repaint();
}

void InstrumentPanel::paint (Graphics& g)
{
    g.fillAll (ground);

    const auto l = layoutOf (getLocalBounds());

    drawHeader (g, l.header);

    if (isArp())
    {
        drawChordRing  (g, l.ring);
        drawArpPlate   (g, l.plate);
        drawArpLevels  (g, l.triggers);
        drawArpReadout (g, l.readout);
        return;
    }

    if (isStrings())
    {
        drawStringRing    (g, l.ring);
        drawStringBow     (g, l.plate);
        drawStringLevels  (g, l.triggers);
        drawStringReadout (g, l.readout);
        return;
    }

    if (isAcid())
    {
        drawAcidStep     (g, l.ring);
        drawAcidSequence (g, l.plate);
        drawAcidLevels   (g, l.triggers);
        drawAcidReadout  (g, l.readout);
        return;
    }

    if (isJuno())
    {
        drawChordRing   (g, l.ring);
        drawJunoPhrase  (g, l.plate);
        drawJunoLevels  (g, l.triggers);
        drawJunoReadout (g, l.readout);
        return;
    }

    if (isTheremin())
    {
        drawScaleRuler      (g, l.ring);
        drawTone            (g, l.plate);
        drawThereminLevels  (g, l.triggers);
        drawThereminReadout (g, l.readout);
        return;
    }

    drawChordRing (g, l.ring);
    drawPlate     (g, l.plate);
    drawTriggers  (g, l.triggers);
    drawReadout   (g, l.readout);
}

//== theremin ================================================================

void InstrumentPanel::drawScaleRuler (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "SKALA OCH TONHOJD");

    const auto& p = processor.published();

    const int   keyRoot  = p.keyRoot.load (std::memory_order_relaxed);
    const int   scale    = p.scaleIndex.load (std::memory_order_relaxed);
    const int   octaves  = p.rangeOctaves.load (std::memory_order_relaxed);
    const float raw      = p.rawMidi.load (std::memory_order_relaxed);
    const float sounding = p.soundingMidi.load (std::memory_order_relaxed);

    const auto& definition = music::scaleAt (scale);

    auto body = area.reduced (16).withTrimmedTop (24);

    g.setColour (muted);
    g.setFont (mono (9.5f, true));
    g.drawText (p.thereminMotion.load (std::memory_order_relaxed)
                  ? String ("HOGER HAND: PEKA UPP TONHOJD, VRID BOJ")
                  : buttonName ("HOGER SPAK") + " Y TONHOJD   X BOJ",
                body.removeFromBottom (14), Justification::centredLeft);
    body.removeFromBottom (8);

    // The whole reachable range, bottom to top, so where the thumb is and where
    // the scale is pulling it are the same picture.
    const int low  = 48 + keyRoot + 12 * (p.octave.load (std::memory_order_relaxed));
    const int high = low + 12 * octaves;

    auto yFor = [&body, low, high] (float note)
    {
        const float t = (note - (float) low) / (float) std::max (1, high - low);
        return (float) body.getBottom() - jlimit (0.0f, 1.0f, t) * (float) body.getHeight();
    };

    // Every degree of the scale in the range, with the roots picked out.
    for (int note = low; note <= high; ++note)
    {
        const int degree = ((note - low) % 12 + 12) % 12;

        if (((definition.mask >> degree) & 1u) == 0u)
            continue;

        const bool root = (degree == 0);
        const auto y = yFor ((float) note);

        g.setColour (root ? edge : hair);
        g.drawLine ((float) body.getX() + (root ? 0.0f : 26.0f), y,
                    (float) body.getRight() - 70.0f, y, root ? 1.6f : 1.0f);

        if (! root)
            continue;

        g.setColour (muted);
        g.setFont (mono (10.0f));
        g.drawText (String (music::pitchClassName (note % 12)) + String (note / 12 - 1),
                    Rectangle<float> ((float) body.getRight() - 64.0f, y - 8.0f, 60.0f, 16.0f),
                    Justification::centredLeft);
    }

    // Where the stick actually points, and where it ends up. The gap between
    // them is the pull, made visible — which is the only way to know what the
    // right trigger is doing.
    const auto rawY = yFor (raw);
    const auto outY = yFor (sounding);

    g.setColour (muted.withAlpha (0.5f));
    g.drawLine ((float) body.getX(), rawY, (float) body.getRight() - 70.0f, rawY, 1.5f);

    g.setColour (accent);
    g.drawLine ((float) body.getX(), outY, (float) body.getRight() - 70.0f, outY, 3.0f);
    g.fillEllipse ((float) body.getX() - 3.0f, outY - 5.0f, 10.0f, 10.0f);
}

void InstrumentPanel::drawTone (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "TON");

    const auto& p = processor.published();

    const float sounding = p.soundingMidi.load (std::memory_order_relaxed);
    const float cents    = p.centsOff.load (std::memory_order_relaxed);
    const bool  frozen   = p.pitchFrozen.load (std::memory_order_relaxed);
    const bool  latched  = p.chordLatched.load (std::memory_order_relaxed);
    const float level    = Decibels::decibelsToGain (
                              p.masterDb.load (std::memory_order_relaxed),
                              processor.tuning().triggerFloorDb);

    auto body = area.reduced (16).withTrimmedTop (18);
    auto head = body.removeFromTop (58);

    const int note = (int) std::lround (sounding);

    g.setColour (level > 0.002f ? accent : edge);
    g.setFont (mono (44.0f, true));
    g.drawText (noteName (note), head, Justification::centredLeft);

    if (frozen || latched)
    {
        auto badge = head.removeFromRight (96).withSizeKeepingCentre (92, 22);

        g.setColour (accent);
        g.fillRect (badge);
        g.setColour (ground);
        g.setFont (mono (11.0f, true));
        g.drawText (frozen ? "FRYST " + buttonName ("LB") : String ("LAST LS"), badge, Justification::centred);
    }

    body.removeFromTop (6);

    // Intonation, which is the thing you cannot hear yourself doing until it is
    // too late. Centre is in tune; the bar walks off it.
    auto meter = body.removeFromTop (30);
    const auto centre = (float) meter.getCentreX();
    const auto span = (float) meter.getWidth() * 0.5f - 10.0f;

    g.setColour (hair);
    g.fillRect (meter.reduced (6, 12));

    g.setColour (edge);
    g.drawLine (centre, (float) meter.getY() + 4.0f, centre, (float) meter.getBottom() - 4.0f, 1.0f);

    const auto offset = jlimit (-1.0f, 1.0f, cents / 50.0f) * span;

    g.setColour (std::abs (cents) < 8.0f ? accent : muted);
    g.fillRect (Rectangle<float> (centre + offset - 2.0f, (float) meter.getY() + 2.0f,
                                  4.0f, (float) meter.getHeight() - 4.0f));

    body.removeFromTop (4);

    g.setColour (muted);
    g.setFont (mono (11.0f));
    g.drawText (String (cents >= 0.0f ? "+" : "") + String (cents, 0) + " cent",
                body.removeFromTop (16), Justification::centredLeft);

    g.drawText (String (p.rangeOctaves.load (std::memory_order_relaxed)) + " oktavers omfang (" + buttonName ("Y") + ")"
                  + "   " + buttonName ("MENY") + " glid " + String (p.glideIndex.load (std::memory_order_relaxed) == 0 ? "kort"
                                       : p.glideIndex.load (std::memory_order_relaxed) == 1 ? "mjuk"
                                                                                            : "lang"),
                body.removeFromTop (16), Justification::centredLeft);

    // The buttons with no other place on this screen, named.
    body.removeFromTop (12);

    g.drawText ("VANSTER SPAK: UPP VIBRATO, NER TREMOLO, X FART",
                body.removeFromTop (16), Justification::centredLeft);
    g.drawText (buttonName ("LB") + " FRYS TONEN   LS LAS TONEN   " + buttonName ("VIEW")
                  + " OKTAV   RS TYSTA",
                body.removeFromTop (16), Justification::centredLeft);

    // Motion: what RB does here, or why it does nothing.
    const bool available = p.motionAvailable.load (std::memory_order_relaxed);
    const bool moving    = p.thereminMotion.load (std::memory_order_relaxed);

    body.removeFromTop (8);

    g.setColour (moving ? accent : muted);
    g.setFont (mono (11.0f, moving));
    g.drawText (! available ? String ("RORELSE: KRAVER JOY-CON, SWITCH PRO ELLER STEAM CONTROLLER")
                            : moving ? buttonName ("RB") + " RORELSE PA   HEM = GREPPET NAR DU SLOG PA"
                                     : buttonName ("RB") + " SLA PA RORELSE",
                body.removeFromTop (16), Justification::centredLeft);
}

void InstrumentPanel::drawThereminLevels (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "NIVAER");

    const auto& t = processor.tuning();
    const auto& p = processor.published();

    const auto lt = pad::applyDeadzone1D (pad.lt, t.triggerDeadzone);
    const auto rt = pad::applyDeadzone1D (pad.rt, t.triggerDeadzone);

    auto bar = [&g] (Rectangle<int> row, float value, const String& label, const String& reading)
    {
        g.setColour (muted);
        g.setFont (mono (10.5f, true));
        g.drawText (label, row.removeFromLeft (kBarLabel), Justification::centredLeft);

        g.setColour (ink);
        g.setFont (mono (12.0f, true));
        g.drawText (reading, row.removeFromRight (kBarReading), Justification::centredRight);

        const auto track = row.reduced (6, 7);

        g.setColour (hair);
        g.fillRect (track);
        g.setColour (accent);
        g.fillRect (track.withWidth (roundToInt ((float) track.getWidth()
                                                 * jlimit (0.0f, 1.0f, value))));
        g.setColour (edge);
        g.drawRect (track, 1);
    };

    // With a Joy-Con pair in motion mode the left hand is the volume, so the bar
    // follows the hand rather than a trigger nobody is pressing.
    const auto hand  = p.motionVolume.load (std::memory_order_relaxed);
    const auto swell = hand >= 0.0f ? hand : lt;

    bar (levelRow (area, 0), swell, hand >= 0.0f ? String ("HAND") : buttonName ("LT"),
         swell <= 0.0f ? String ("tyst")
                       : String (pad::triggerDecibels (swell, t), 1) + " dB");

    const auto lock = p.quantise.load (std::memory_order_relaxed);

    bar (levelRow (area, 1), rt, buttonName ("RT"),
         lock < 0.02f ? String ("fri")
                      : (lock > 0.98f ? String ("fast")
                                      : String (roundToInt (lock * 100.0f)) + " % drag"));

    const auto vibrato = p.vibrato.load (std::memory_order_relaxed);
    const auto tremolo = p.tremolo.load (std::memory_order_relaxed);

    bar (levelRow (area, 2), std::max (vibrato, tremolo), "MOD",
         (vibrato > tremolo ? String ("vibrato ") : String ("tremolo "))
           + String (p.lfoRate.load (std::memory_order_relaxed), 1) + " Hz");
}

void InstrumentPanel::drawThereminReadout (Graphics& g, Rectangle<int> area)
{
    const auto& p = processor.published();

    const int scale = p.scaleIndex.load (std::memory_order_relaxed);
    const float lock = p.quantise.load (std::memory_order_relaxed);

    const std::vector<std::pair<String, String>> cells
    {
        { p.thereminMotion.load (std::memory_order_relaxed) ? String ("H HAND  TON") : buttonName ("H SPAK") + "  TON", noteName ((int) std::lround (p.soundingMidi.load (std::memory_order_relaxed))) },
        { buttonName ("D-PAD") + " UPP/NER  TONART", String (music::pitchClassName (p.keyRoot.load (std::memory_order_relaxed))) },
        { buttonName ("A") + " " + buttonName ("B") + " " + buttonName ("X") + "  SKALA", String (music::scaleAt (scale).name) },
        { buttonName ("D-PAD") + " SIDLED  KLANG", String (dsp::thereminTimbre (p.timbreIndex.load (std::memory_order_relaxed)).name) },
        { buttonName ("RT") + "  LASNING", lock < 0.02f ? String ("FRI")
                                  : String (roundToInt (lock * 100.0f)) + "%" }
    };

    g.setColour (panel);
    g.fillRect (area);
    g.setColour (hair);
    g.drawRect (area, 1);

    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto cell = readoutCell (area, (int) i, (int) cells.size());

        if (i > 0)
        {
            g.setColour (hair);
            g.drawLine ((float) cell.getX(), (float) cell.getY() + 10.0f,
                        (float) cell.getX(), (float) cell.getBottom() - 10.0f, 1.0f);
        }

        auto inner = cell.reduced (8, 14);

        g.setColour (muted);
        g.setFont (mono (9.5f, true));
        g.drawText (cells[i].first, inner.removeFromTop (14), Justification::centred);

        g.setColour (i == 0 ? accent : ink);
        g.setFont (mono (i == 0 ? 28.0f : 18.0f, true));
        g.drawText (cells[i].second, inner, Justification::centred);
    }
}

void InstrumentPanel::refreshMidiPorts()
{
    const auto devices = MidiOutput::getAvailableDevices();

    StringArray ids;

    for (const auto& device : devices)
        ids.add (device.identifier);

    if (ids != midiPortIds || midiPortBox.getNumItems() == 0)
    {
        midiPortIds = ids;
        midiPortBox.clear (dontSendNotification);
        midiPortBox.addItem ("MIDI UT: AV", 1);

        for (int i = 0; i < devices.size(); ++i)
            midiPortBox.addItem ("MIDI UT: " + devices[i].name, i + 2);
    }

    const int chosen = midiPortIds.indexOf (processor.midiPortIdentifier());
    midiPortBox.setSelectedId (chosen >= 0 ? chosen + 2 : 1, dontSendNotification);
}

void InstrumentPanel::drawHeader (Graphics& g, Rectangle<int> area)
{
    g.setColour (accent);
    g.fillRect (area.removeFromBottom (2));

    auto left = area.removeFromLeft (area.getWidth() / 2);

    g.setColour (ink);
    g.setFont (mono (22.0f, true));
    g.drawText ("CHORDTROLLER", left.removeFromTop (28), Justification::bottomLeft);

    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("OMNICHORD FOR HANDKONTROLL", left, Justification::topLeft);

    // The one piece of hardware truth a player still needs — whether the thing
    // in their hands is being heard — on the subtitle's row, since the right of
    // the header now holds both menus.
    auto status = left.removeFromTop (14).withTrimmedLeft (200).withTrimmedRight (16);
    auto dot    = status.removeFromRight (14).withSizeKeepingCentre (9, 9);

    g.setColour (pad.connected ? accent : edge);
    g.fillEllipse (dot.toFloat());

    g.setColour (pad.connected ? muted : accent);
    g.setFont (mono (10.5f, true));
    g.drawText (! pad.connected ? String ("INGEN KONTROLL")
                                : String (pad.layout == PadState::Layout::nintendo ? "NINTENDO ANSLUTEN"
                                        : pad.layout == PadState::Layout::steam    ? "STEAM CONTROLLER ANSLUTEN"
                                                                                   : "ANSLUTEN"),
                status.withTrimmedRight (6), Justification::centredRight);
}

void InstrumentPanel::drawChordRing (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "ACKORDRING");

    const auto& published = processor.published();
    const int sector = published.sector.load (std::memory_order_relaxed);
    const bool latched = published.chordLatched.load (std::memory_order_relaxed);

    auto body = area.reduced (16).withTrimmedTop (16);

    // What moves the ring, named under it. Claimed before the ring is sized, so
    // the bottom chord's name cannot land on top of it.
    g.setColour (muted);
    g.setFont (mono (9.5f, true));
    g.drawText (isJuno() ? "V SPAK ACKORD     LB UTOKADE     LS LAS"
                         : "V SPAK ACKORD     LB SEPTIMACKORD     LS LAS",
                body.removeFromBottom (14), Justification::centred);

    const auto cx = (float) body.getCentreX();
    const auto cy = (float) body.getCentreY();

    // Sized from the room the labels need rather than as a fraction of the
    // panel: the names sit outside the circle, and a ring scaled by a fraction
    // pushes "Bbmaj7" through the panel wall as soon as the window narrows.
    constexpr float labelGap  = 24.0f;
    constexpr float labelHalf = 35.0f;

    const auto r = jmax (56.0f, jmin (body.getWidth(), body.getHeight()) * 0.5f
                                  - labelGap - labelHalf);

    auto pointAt = [cx, cy] (int index, float radius)
    {
        const float rad = (float) index * 45.0f * pad::kPi / 180.0f;
        return Point<float> (cx + std::sin (rad) * radius, cy - std::cos (rad) * radius);
    };

    g.setColour (hair);
    g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);

    // Where the left thumb is pointing, if it is pointing anywhere. Not a raw
    // number — the aim itself, which is the thing a hand can correct.
    const auto& t = processor.tuning();
    const auto dz = pad::applyDeadzone (pad.lx, pad.ly, t.stickDeadzone);
    const auto sq = pad::discToSquare (dz.x, dz.y);
    const float aim = std::sqrt (sq.x * sq.x + sq.y * sq.y);

    if (aim >= t.chordHoldRadius && ! latched)
    {
        g.setColour (accent.withAlpha (0.35f));
        g.drawLine (cx, cy, cx + sq.x * r * 0.86f, cy - sq.y * r * 0.86f, 2.0f);
    }

    for (int i = 0; i < music::ChordRing::kSectors; ++i)
    {
        const bool active = (i == sector);
        const auto dot = pointAt (i, r);

        g.setColour (active ? accent : edge);

        if (active)
            g.fillEllipse (dot.x - 8.0f, dot.y - 8.0f, 16.0f, 16.0f);
        else
            g.drawEllipse (dot.x - 4.5f, dot.y - 4.5f, 9.0f, 9.0f, 1.5f);

        const auto label = pointAt (i, r + labelGap);

        g.setColour (active ? accent : muted);
        g.setFont (mono (active ? 15.0f : 13.0f, active));
        g.drawText (ringName (i),
                    Rectangle<float> (label.x - labelHalf, label.y - 10.0f,
                                      labelHalf * 2.0f, 20.0f),
                    Justification::centred);
    }

    // A mode that freezes the instrument has to be impossible to miss. Whispering
    // it in ten point grey was how the latch could be on without anyone knowing,
    // which reads as the instrument having stopped following the stick.
    if (latched)
    {
        auto badge = Rectangle<float> (cx - 46.0f, cy - 12.0f, 92.0f, 24.0f);

        g.setColour (accent);
        g.fillRect (badge);

        g.setColour (ground);
        g.setFont (mono (12.0f, true));
        g.drawText ("LAST  LS", badge, Justification::centred);
    }
    else
    {
        g.setColour (muted);
        g.setFont (mono (10.0f));
        g.drawText (isJuno() ? String (music::genreName (published.junoGenre.load (std::memory_order_relaxed)))
                                 + (published.junoExtended.load (std::memory_order_relaxed) ? " +LB" : "")
                             : String (ring.bankName()).toUpperCase(),
                    Rectangle<float> (cx - 60.0f, cy - 9.0f, 120.0f, 18.0f),
                    Justification::centred);
    }
}

void InstrumentPanel::drawPlate (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "PLATTAN");

    const auto& published = processor.published();
    const int index  = published.stringIndex.load (std::memory_order_relaxed);
    const int count  = published.stringCount.load (std::memory_order_relaxed);
    const int note   = published.stringMidi.load (std::memory_order_relaxed);
    const int zoneLo = published.zoneLow.load (std::memory_order_relaxed);
    const int zoneHi = published.zoneHigh.load (std::memory_order_relaxed);

    auto body = area.reduced (16).withTrimmedTop (18);

    // The note that just sounded, big. On a plate whose order is not pitch
    // order, this is the only way to see the pattern rolling.
    auto head = body.removeFromTop (52);

    g.setColour (note < 0 ? edge : accent);
    g.setFont (mono (40.0f, true));
    g.drawText (note < 0 ? String ("--") : noteName (note), head,
                Justification::centredLeft);

    const bool whole = (zoneLo <= 0 && zoneHi >= count - 1);

    g.setColour (muted);
    g.setFont (mono (11.0f));
    g.drawText ((whole ? String ("hela plattan")
                       : String (zoneHi - zoneLo + 1) + " av " + String (count)
                           + (zoneLo == 0 ? ", lagt" : ", hogt"))
                  + "   hoger spak X",
                head, Justification::centredRight);

    body.removeFromTop (10);

    // Which way and how fast, claimed before the strings so a short panel eats
    // into the string height rather than dropping the reading off the bottom.
    auto foot = body.removeFromBottom (26);
    body.removeFromBottom (12);

    // The buttons with no other place on this screen, named. The strings give
    // up the height; they can spare it far better than a player can spare
    // guessing.
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("Y DUR/MOLL   VIEW OKTAV   A B X KOMPFIGUR   MENY TRUMMOR   RS TYSTA ALLT",
                body.removeFromBottom (16), Justification::centredLeft);
    body.removeFromBottom (8);

    // The strings, in plate order. Anything the octave zone has shut out is
    // dimmed, so the window the right thumb chose is visible rather than
    // inferred.
    auto strings = body;

    if (count > 0)
    {
        const float w = (float) strings.getWidth() / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const auto x = (float) strings.getX() + w * ((float) i + 0.5f);
            const bool lit    = (i == index);
            const bool inZone = (i >= zoneLo && i <= zoneHi);

            g.setColour (lit ? accent : (inZone ? edge : hair));
            g.drawLine (x, (float) strings.getY(), x, (float) strings.getBottom(),
                        lit ? 3.5f : (inZone ? 1.5f : 1.0f));
        }
    }

    // Which way and how fast, which is what the right thumb is actually doing.
    const auto& t = processor.tuning();
    const auto dz = pad::applyDeadzone (pad.rx, pad.ry, t.stickDeadzone);
    const auto sq = pad::discToSquare (dz.x, dz.y);
    const auto rate = pad::strumRate (sq.y, t);

    foot.removeFromRight (128);     // the vibration tick sits here
    auto arrow = foot.removeFromLeft (28).toFloat();

    if (rate > 0.0f)
    {
        Path p;
        const float mid = arrow.getCentreX();
        const float top = arrow.getCentreY() - 8.0f;
        const float bot = arrow.getCentreY() + 8.0f;

        if (sq.y > 0.0f)
        {
            p.addTriangle (mid, top, mid - 7.0f, bot, mid + 7.0f, bot);
        }
        else
        {
            p.addTriangle (mid, bot, mid - 7.0f, top, mid + 7.0f, top);
        }

        g.setColour (accent);
        g.fillPath (p);
    }

    g.setColour (rate > 0.0f ? ink : muted);
    g.setFont (mono (13.0f, rate > 0.0f));
    g.drawText ((rate > 0.0f ? String (rate, 1) + " ton/s" : String ("stilla"))
                  + "   hoger spak Y",
                foot, Justification::centredLeft);
}

void InstrumentPanel::drawTriggers (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "NIVAER");

    const auto& t = processor.tuning();

    const auto lt = pad::applyDeadzone1D (pad.lt, t.triggerDeadzone);
    const auto rt = pad::applyDeadzone1D (pad.rt, t.triggerDeadzone);

    // The first two bars report what the triggers are doing; the third is a
    // control. Handles on the third rather than a flat fill, so it reads as
    // something to grab instead of something to watch.
    auto bar = [&g] (Rectangle<int> row, float value, const String& label,
                     const String& reading, bool interactive)
    {
        g.setColour (interactive ? ink : muted);
        g.setFont (mono (10.5f, true));
        g.drawText (label, row.removeFromLeft (kBarLabel), Justification::centredLeft);

        g.setColour (ink);
        g.setFont (mono (12.0f, true));
        g.drawText (reading, row.removeFromRight (kBarReading), Justification::centredRight);

        // Same final step as barTrack(), which is what the mouse handler uses.
        const auto track = row.reduced (6, 7);

        g.setColour (hair);
        g.fillRect (track);

        const auto filled = track.withWidth (roundToInt ((float) track.getWidth()
                                                         * jlimit (0.0f, 1.0f, value)));
        g.setColour (accent);
        g.fillRect (filled);

        g.setColour (interactive ? muted : edge);
        g.drawRect (track, 1);

        if (! interactive)
            return;

        g.setColour (ink);
        g.fillRect (Rectangle<int> (filled.getRight() - 2, track.getY() - 3, 4,
                                    track.getHeight() + 6));
    };

    bar (levelRow (area, 0), lt, "LT",
         String (pad::ringSeconds (lt, t), 2) + " s ring", false);

    bar (levelRow (area, 1), rt, "RT",
         rt <= 0.0f ? String ("tyst")
                    : String (pad::triggerDecibels (rt, t), 1) + " dB", false);

    const auto level = processor.compLevel();

    bar (levelRow (area, 2), level, "KOMP", levelReading (level), true);
}

void InstrumentPanel::drawReadout (Graphics& g, Rectangle<int> area)
{
    const auto& published = processor.published();

    const int  sector  = published.sector.load (std::memory_order_relaxed);
    const int  voice   = published.voiceIndex.load (std::memory_order_relaxed);
    const int  octave  = published.octave.load (std::memory_order_relaxed);
    const bool running = published.compRunning.load (std::memory_order_relaxed);
    const auto figure  = (dsp::CompPattern) published.compPattern.load (std::memory_order_relaxed);
    const auto bpm     = published.tempo.load (std::memory_order_relaxed);
    const bool fromHost = published.tempoFromHost.load (std::memory_order_relaxed);

    // Five cells, and all five are true. The handoff's strip had Tempo and Komp
    // sitting dead next to two that worked; now they have something to say.
    const std::vector<std::pair<String, String>> cells
    {
        { "V SPAK  ACKORD", String (ring.nameAt (sector)) },
        { "D-PAD UPP/NER, Y  TONART", String (ring.keyName()) },
        { "D-PAD SIDLED  ROST", String (dsp::voiceSpec (voice).name)
                      + (octave != 0 ? String (octave > 0 ? " +" : " ") + String (octave)
                                     : String()) },
        { running ? "RB  KOMP" : "RB  KOMP AV",
          String (dsp::compPatternName (figure))
            + (published.drumsOn.load (std::memory_order_relaxed) ? "+TR" : "") },
        { fromHost ? "TEMPO VARD" : "TEMPO", String ((int) std::lround (bpm)) }
    };

    g.setColour (panel);
    g.fillRect (area);
    g.setColour (hair);
    g.drawRect (area, 1);

    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto cell = readoutCell (area, (int) i, (int) cells.size());

        if (i > 0)
        {
            g.setColour (hair);
            g.drawLine ((float) cell.getX(), (float) cell.getY() + 10.0f,
                        (float) cell.getX(), (float) cell.getBottom() - 10.0f, 1.0f);
        }

        auto inner = cell.reduced (8, 14);

        g.setColour (muted);
        g.setFont (mono (9.5f, true));
        g.drawText (cells[i].first, inner.removeFromTop (14), Justification::centred);

        // The tempo cell's value is a Label, not paint — it has to be clickable.
        if (i == 4)
            continue;

        const bool dim = (i == 3 && ! running);

        g.setColour (i == 0 ? accent : (dim ? edge : ink));
        g.setFont (mono (i == 0 ? 28.0f : 20.0f, true));
        g.drawText (cells[i].second, inner, Justification::centred);
    }
}

void InstrumentPanel::mouseDown (const MouseEvent& e)
{
    const auto l = layoutOf (getLocalBounds());

    // The comp level is a bar, so it takes the position clicked rather than
    // needing to be dragged from wherever it happens to sit.
    const auto track = barTrack (levelRow (l.triggers, 2));

    draggingComp = ! isTheremin() && track.expanded (0, 6).contains (e.getPosition());

    if (draggingComp)
        processor.beginLevelDrag();

    if (draggingComp)
    {
        setCompFromMouse (e.getPosition().x, track);
        return;
    }

    const auto tempoCell = readoutCell (l.readout, 4, 5);

    // Only where the tempo is showing: the theremin and the strings have
    // something else in that cell, and a drag there changed a tempo nobody
    // could see.
    draggingTempo = tempoField.isVisible()
                      && tempoCell.contains (e.getPosition())
                      && ! processor.published().tempoFromHost.load (std::memory_order_relaxed);

    if (draggingTempo)
        tempoAtDragStart = processor.internalTempo();
}

void InstrumentPanel::mouseDrag (const MouseEvent& e)
{
    if (draggingComp)
    {
        setCompFromMouse (e.getPosition().x,
                          barTrack (levelRow (layoutOf (getLocalBounds()).triggers, 2)));
        return;
    }

    if (! draggingTempo)
        return;

    // Upwards is faster, four pixels to the beat. A host that is telling us a
    // tempo owns it, so this is refused rather than silently ignored — see
    // mouseDown.
    processor.setInternalTempo (tempoAtDragStart - (double) e.getDistanceFromDragStartY() * 0.25);
}

void InstrumentPanel::mouseUp (const MouseEvent&)
{
    if (draggingComp)
        processor.endLevelDrag();

    draggingComp = false;
    draggingTempo = false;
}

void InstrumentPanel::setCompFromMouse (int x, Rectangle<int> track)
{
    // The third row is the accompaniment level in Chordtroller, the output
    // level in the Juno, and a readout in the theremin — so only two of the
    // three are something to grab.
    if (track.getWidth() <= 0 || isTheremin())
        return;

    const float value = (float) (x - track.getX()) / (float) track.getWidth();

    processor.setLevelFromPanel (value);
}

//== juno ====================================================================

String InstrumentPanel::ringName (int sector) const
{
    if (! isJuno())
        return String (ring.nameAt (sector));

    const auto& p = processor.published();

    return String (music::junoChordName (ring.chordAt (sector),
                                         p.junoGenre.load (std::memory_order_relaxed),
                                         p.junoExtended.load (std::memory_order_relaxed)));
}

void InstrumentPanel::drawJunoPhrase (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "FRAS");

    const auto& p = processor.published();

    const int  playingStyle = p.junoStyle.load (std::memory_order_relaxed);
    const int  lastStyle    = p.junoLastStyle.load (std::memory_order_relaxed);
    const int  variation    = p.junoVariation.load (std::memory_order_relaxed);
    const bool hold         = p.junoHold.load (std::memory_order_relaxed);
    const bool playing      = playingStyle >= 0;

    const auto& style = dsp::phraseStyle (lastStyle);

    auto body = area.reduced (16).withTrimmedTop (18);

    // What is playing, big. Grey when the thumb is home: the name is still the
    // style the next push will start, so it stays on screen.
    auto head = body.removeFromTop (38);

    if (hold)
    {
        auto badge = head.removeFromRight (96).withSizeKeepingCentre (92, 22);

        g.setColour (accent);
        g.fillRect (badge);
        g.setColour (ground);
        g.setFont (mono (11.0f, true));
        g.drawText ("HALL  RS", badge, Justification::centred);
    }

    g.setColour (playing ? accent : muted);
    g.setFont (mono (26.0f, true));
    g.drawText (String (style.name) + "  " + style.variations[(size_t) variation].name,
                head, Justification::centredLeft);

    body.removeFromTop (8);

    // The eight styles, laid out as the stick is: north first, then clockwise.
    auto grid = body.removeFromTop (56);
    const int cellW = grid.getWidth() / 4;

    for (int i = 0; i < dsp::kNumStyles; ++i)
    {
        auto cell = Rectangle<int> (grid.getX() + (i % 4) * cellW, grid.getY() + (i / 4) * 28,
                                    cellW - 4, 24);

        const bool lit  = playing && i == playingStyle;
        const bool next = ! playing && i == lastStyle;

        g.setColour (lit ? accent : hair);
        g.fillRect (cell);

        if (next)
        {
            g.setColour (accent);
            g.drawRect (cell, 1);
        }

        g.setColour (lit ? ground : (next ? ink : muted));
        g.setFont (mono (10.5f, true));
        g.drawText (String (pad::sectorName (i)) + " " + dsp::phraseStyle (i).name,
                    cell.reduced (6, 0), Justification::centredLeft);
    }

    body.removeFromTop (8);

    // The four variations MENY steps through, for the style it would change.
    auto row = body.removeFromTop (22);
    const int varW = row.getWidth() / dsp::kNumVariations;

    for (int i = 0; i < dsp::kNumVariations; ++i)
    {
        auto cell = row.withX (row.getX() + i * varW).withWidth (varW - 4);
        const bool selected = (i == variation);

        g.setColour (selected ? ink : edge);
        g.drawRect (cell, selected ? 2 : 1);

        g.setColour (selected ? ink : muted);
        g.setFont (mono (10.0f, selected));
        g.drawText (style.variations[(size_t) i].name, cell, Justification::centred);
    }

    // Status, then the keyboard fills whatever is left — the voicing is the one
    // thing on this panel you can see changing while you play.
    auto status = body.removeFromBottom (16);
    body.removeFromTop (10);

    // The buttons with no other place on this screen, named, just above the
    // status line. The keyboard gives up the height.
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("HOGER SPAK STIL   MENY VARIATION   RS HALL FRASEN   RB TYSTA ALLT",
                body.removeFromBottom (16), Justification::centredLeft);
    body.removeFromBottom (4);

    static const char* chorusNames[4] { "AV", "I", "II", "I+II" };
    const int octave = p.junoOctave.load (std::memory_order_relaxed);

    g.setColour (muted);
    g.setFont (mono (10.5f));
    g.drawText ("A CHORUS " + String (chorusNames[jlimit (0, 3, p.junoChorus.load (std::memory_order_relaxed))])
                  + "   X BAS " + (p.junoBass.load (std::memory_order_relaxed) ? "PA" : "AV")
                  + "   VIEW OKTAV " + (octave > 0 ? "+" : "") + String (octave),
                status, Justification::centredLeft);

    body.removeFromBottom (8);
    drawKeyboard (g, body);
}

void InstrumentPanel::drawKeyboard (Graphics& g, Rectangle<int> area)
{
    const auto& p = processor.published();

    std::array<bool, 128> lit {};

    for (int i = 0; i < p.junoSoundingCount.load (std::memory_order_relaxed) && i < 6; ++i)
    {
        const int note = p.junoSounding[i].load (std::memory_order_relaxed);

        if (note >= 0 && note < 128)
            lit[(size_t) note] = true;
    }

    // C2 to C6: where the bass and every voicing the Juno leads can land.
    constexpr int first = 36, last = 84;
    constexpr int whiteOf[12] { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    constexpr bool black[12]  { false, true, false, true, false, false, true, false, true, false, true, false };

    const int whites = 7 * ((last - first) / 12) + 1;
    const float w = (float) area.getWidth() / (float) whites;
    const float top = (float) area.getY();
    const float height = (float) area.getHeight();

    auto xOf = [&] (int note)
    {
        const int octaves = (note - first) / 12;
        return (float) area.getX() + w * (float) (octaves * 7 + whiteOf[note % 12]);
    };

    for (int note = first; note <= last; ++note)
    {
        if (black[note % 12])
            continue;

        const auto key = Rectangle<float> (xOf (note), top, w - 1.0f, height);

        g.setColour (lit[(size_t) note] ? accent : Colour (0xffd8d1cb));
        g.fillRect (key);
    }

    for (int note = first; note <= last; ++note)
    {
        if (! black[note % 12])
            continue;

        const auto key = Rectangle<float> (xOf (note) + w * 0.62f, top, w * 0.72f, height * 0.6f);

        g.setColour (lit[(size_t) note] ? accent : ground);
        g.fillRect (key);
    }
}

void InstrumentPanel::drawJunoLevels (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "NIVAER");

    const auto& p = processor.published();

    auto bar = [&g] (Rectangle<int> row, float value, const String& label,
                     const String& reading, bool interactive)
    {
        g.setColour (interactive ? ink : muted);
        g.setFont (mono (10.5f, true));
        g.drawText (label, row.removeFromLeft (kBarLabel), Justification::centredLeft);

        g.setColour (ink);
        g.setFont (mono (12.0f, true));
        g.drawText (reading, row.removeFromRight (kBarReading), Justification::centredRight);

        const auto track = row.reduced (6, 7);

        g.setColour (hair);
        g.fillRect (track);

        const auto filled = track.withWidth (roundToInt ((float) track.getWidth()
                                                         * jlimit (0.0f, 1.0f, value)));

        g.setColour (accent);
        g.fillRect (filled);

        g.setColour (interactive ? muted : edge);
        g.drawRect (track, 1);

        if (interactive)
        {
            g.setColour (ink);
            g.fillRect (Rectangle<int> (filled.getRight() - 2, track.getY() - 3, 4,
                                        track.getHeight() + 6));
        }
    };

    // Both start at zero and the trigger owns the whole bar, so what is drawn is
    // simply where the finger is.
    const auto cutoff = p.junoCutoff.load (std::memory_order_relaxed);
    const auto reso   = p.junoResonance.load (std::memory_order_relaxed);

    bar (levelRow (area, 0), cutoff, "RT",
         "cutoff " + String (roundToInt (cutoff * 100.0f)) + " %", false);

    bar (levelRow (area, 1), reso, "LT",
         "reso " + String (roundToInt (reso * 100.0f)) + " %", false);

    const auto level = processor.junoLevel();

    bar (levelRow (area, 2), level, "NIVA", levelReading (level), true);
}

void InstrumentPanel::drawJunoReadout (Graphics& g, Rectangle<int> area)
{
    const auto& p = processor.published();

    const int sector = p.sector.load (std::memory_order_relaxed);

    const std::vector<std::pair<String, String>> cells
    {
        { "V SPAK  ACKORD", ringName (sector) },
        { "D-PAD UPP/NER, Y  TONART", String (ring.keyName()) },
        { "D-PAD SIDLED  PATCH", String (dsp::junoPatch (p.junoPatch.load (std::memory_order_relaxed)).name) },
        { "B  GENRE", String (music::genreName (p.junoGenre.load (std::memory_order_relaxed))) },
        { p.tempoFromHost.load (std::memory_order_relaxed) ? "TEMPO VARD" : "TEMPO", String() }
    };

    g.setColour (panel);
    g.fillRect (area);
    g.setColour (hair);
    g.drawRect (area, 1);

    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto cell = readoutCell (area, (int) i, (int) cells.size());

        if (i > 0)
        {
            g.setColour (hair);
            g.drawLine ((float) cell.getX(), (float) cell.getY() + 10.0f,
                        (float) cell.getX(), (float) cell.getBottom() - 10.0f, 1.0f);
        }

        auto inner = cell.reduced (8, 14);

        g.setColour (muted);
        g.setFont (mono (9.5f, true));
        g.drawText (cells[i].first, inner.removeFromTop (14), Justification::centred);

        // The tempo's number is the clickable field, not paint.
        if (i == 4)
            continue;

        g.setColour (i == 0 ? accent : ink);
        g.setFont (mono (i == 0 ? 24.0f : 18.0f, true));
        g.drawFittedText (cells[i].second, inner, Justification::centred, 1, 0.8f);
    }
}

//== 303 =====================================================================

bool InstrumentPanel::isAcid() const
{
    return processor.instrument() == ChordtrollerAudioProcessor::Instrument::acid;
}

void InstrumentPanel::drawAcidStep (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "STEGET");

    const auto& p = processor.published();

    const int  editAt  = jlimit (0, dsp::kAcidMaxSteps - 1, p.acidCursor.load (std::memory_order_relaxed));
    const auto step    = dsp::AcidStep::unpack (p.acidSteps[editAt].load (std::memory_order_relaxed));
    const int  keyRoot = p.keyRoot.load (std::memory_order_relaxed);
    const int  scale   = p.acidScale.load (std::memory_order_relaxed);
    const int  octave  = p.acidOctave.load (std::memory_order_relaxed);
    const bool shift   = p.acidShift.load (std::memory_order_relaxed);

    auto body = area.reduced (16).withTrimmedTop (18);

    // The step under the cursor, and what it plays.
    auto head = body.removeFromTop (32);

    g.setColour (ink);
    g.setFont (mono (22.0f, true));
    g.drawText ("STEG " + String (editAt + 1), head, Justification::centredLeft);

    g.setColour (step.rest ? muted : accent);
    g.drawText (step.rest ? String ("PAUS")
                          : noteName (dsp::acidMidiFor (step.degree, keyRoot, scale, octave)),
                head, Justification::centredRight);

    // Key and scale, named with what changes them in the layer that is up.
    g.setColour (shift ? accent : muted);
    g.setFont (mono (10.5f, shift));
    g.drawText (String (shift ? "UPP/NER" : "LB+UPP/NER") + "  TONART "
                  + String (music::pitchClassName (keyRoot))
                  + "     " + (shift ? "Y" : "LB+Y") + "  " + String (music::scaleAt (scale).name),
                body.removeFromTop (16), Justification::centredLeft);

    body.removeFromTop (10);

    auto ladder = body.removeFromLeft (body.getWidth() * 2 / 5);
    body.removeFromLeft (12);
    auto buttons = body;

    // Left: the notes up and down walk through, low at the bottom, the roots
    // marked so an octave can be found without reading.
    g.setColour (shift ? accent : muted);
    g.setFont (mono (9.5f, true));
    g.drawText (shift ? "UPP NER  TONART" : "D-PAD UPP NER", ladder.removeFromBottom (14),
                Justification::centredLeft);

    const int   top   = dsp::acidTopDegree (scale);
    const int   count = dsp::acidNotesInScale (scale);
    const float rowH  = jmin (15.0f, (float) ladder.getHeight() / (float) (top + 1));
    const int   shown = jlimit (0, top, step.degree);

    for (int d = 0; d <= top; ++d)
    {
        const Rectangle<float> row ((float) ladder.getX(),
                                    (float) ladder.getBottom() - (float) (d + 1) * rowH,
                                    (float) ladder.getWidth(), rowH - 1.0f);

        const bool here = ! step.rest && d == shown;
        const bool root = d % count == 0;

        if (here || root)
        {
            g.setColour (here ? accent : hair);
            g.fillRect (row);
        }

        // Twenty-five chromatic rows leave too little height for a name on every
        // one. Then only the roots and the chosen note are named, and the rest
        // are ticks: an unreadable label is worse than a mark.
        const bool named = rowH >= 11.0f || here || root;

        if (named)
        {
            g.setColour (here ? ground : (root ? ink : muted));
            g.setFont (mono (jlimit (9.0f, 11.0f, rowH - 3.0f), here || root));
            g.drawText (noteName (dsp::acidMidiFor (d, keyRoot, scale, octave)),
                        row.reduced (6.0f, 0.0f), Justification::centredLeft);
        }
        else
        {
            g.setColour (edge);
            g.fillRect (Rectangle<float> (row.getX() + 6.0f, row.getCentreY() - 0.5f, 10.0f, 1.0f));
        }
    }

    // Right: the four face buttons, where they sit on the pad. Without shift
    // they write the step, lit where this step has them. With shift they are
    // the settings, named, and nothing is lit.
    g.setColour (shift ? accent : muted);
    g.setFont (mono (9.5f, true));
    g.drawText (shift ? "LB + KNAPPARNA" : "KNAPPARNA", buttons.removeFromBottom (14),
                Justification::centred);

    const auto cx  = (float) buttons.getCentreX();
    const auto cy  = (float) buttons.getCentreY();
    const auto arm = jmax (30.0f, jmin ((float) buttons.getWidth(), (float) buttons.getHeight()) * 0.5f - 46.0f);

    struct Face { const char* letter; float dx, dy; const char* plain; const char* shifted; bool on; };

    const Face faces[4]
    {
        { "Y",  0.0f, -1.0f, "ACCENT",  "SKALA",     ! step.rest && step.accent },
        { "B",  1.0f,  0.0f, "SLIDE",   "GLIDE",     ! step.rest && step.slide },
        { "A",  0.0f,  1.0f, "PAUS",    "LOOPSLUT",  step.rest },
        { "X", -1.0f,  0.0f, "KOPIERA", "DELAYTAKT", false }
    };

    for (const auto& f : faces)
    {
        const auto box = Rectangle<float> (cx + f.dx * arm - 42.0f, cy + f.dy * arm - 11.0f, 84.0f, 22.0f);
        const bool lit = ! shift && f.on;

        if (lit)
        {
            g.setColour (accent);
            g.fillRect (box);
        }

        g.setColour (lit || shift ? accent : edge);
        g.drawRect (box, 1.0f);

        g.setColour (lit ? ground : (shift ? ink : muted));
        g.setFont (mono (10.5f, true));
        g.drawText (String (f.letter) + " " + (shift ? f.shifted : f.plain), box,
                    Justification::centred);
    }
}

void InstrumentPanel::drawAcidSequence (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "SEKVENS");

    const auto& p = processor.published();

    const int  length   = p.acidLength.load (std::memory_order_relaxed);
    const int  playhead = p.acidPlayhead.load (std::memory_order_relaxed);
    const int  editAt   = p.acidCursor.load (std::memory_order_relaxed);
    const bool running  = p.acidRunning.load (std::memory_order_relaxed);
    const bool square   = p.acidSquare.load (std::memory_order_relaxed);
    const bool shift    = p.acidShift.load (std::memory_order_relaxed);
    const int  keyRoot  = p.keyRoot.load (std::memory_order_relaxed);
    const int  scale    = p.acidScale.load (std::memory_order_relaxed);
    const int  octave   = p.acidOctave.load (std::memory_order_relaxed);

    auto body = area.reduced (16).withTrimmedTop (18);
    auto head = body.removeFromTop (38);

    // Shift has to be impossible to miss: every button means something else.
    if (shift)
    {
        auto badge = head.removeFromRight (96).withSizeKeepingCentre (92, 22);

        g.setColour (accent);
        g.fillRect (badge);
        g.setColour (ground);
        g.setFont (mono (11.0f, true));
        g.drawText ("SHIFT  LB", badge, Justification::centred);
    }

    g.setColour (running ? accent : ink);
    g.setFont (mono (26.0f, true));
    g.drawText (running ? "SPELAR" : "STOPP", head, Justification::centredLeft);

    g.setColour (muted);
    g.setFont (mono (10.5f));
    g.drawText (p.hostPlaying.load (std::memory_order_relaxed) ? "VARDEN STYR TAKTEN"
                  : running ? "MENY STOPPAR" : "MENY SPELAR",
                head.withTrimmedLeft (150),
                Justification::centredLeft);

    body.removeFromTop (8);

    // Two rows of eight, which is how the bar reads: a sixteenth line in two
    // halves, each half in two beats.
    constexpr int rowHeight = 54, rowGap = 6;

    auto grid = body.removeFromTop (rowHeight * 2 + rowGap);
    const int cellW = grid.getWidth() / 8;

    for (int i = 0; i < dsp::kAcidMaxSteps; ++i)
    {
        const auto step = dsp::AcidStep::unpack (p.acidSteps[i].load (std::memory_order_relaxed));

        const auto cell = Rectangle<int> (grid.getX() + (i % 8) * cellW,
                                          grid.getY() + (i / 8) * (rowHeight + rowGap),
                                          cellW - 6, rowHeight);

        const bool inLoop = i < length;
        const bool here   = running && i == playhead;
        const bool marked = i == editAt;

        g.setColour (here ? accent.withAlpha (0.25f) : (inLoop ? hair : panel));
        g.fillRect (cell);

        g.setColour (marked ? ink : (here ? accent : (inLoop ? edge : hair)));
        g.drawRect (cell, marked ? 2 : 1);

        g.setColour (inLoop ? muted : edge);
        g.setFont (mono (9.5f));
        g.drawText (String (i + 1), cell.reduced (5, 3).removeFromTop (12),
                    Justification::topLeft);

        // Accent is a filled corner; slide is a bar reaching into the next step,
        // because that is where it goes; the loop's last step has a bar standing
        // at its end.
        if (! step.rest && step.accent)
        {
            g.setColour (inLoop ? accent : edge);
            g.fillRect (Rectangle<int> (cell.getRight() - 11, cell.getY() + 4, 7, 7));
        }

        if (! step.rest && step.slide)
        {
            g.setColour (inLoop ? accent : edge);
            g.fillRect (Rectangle<int> (cell.getRight() - 8, cell.getBottom() - 10, 14, 3));
        }

        if (i == length - 1)
        {
            g.setColour (accent);
            g.fillRect (Rectangle<int> (cell.getRight() + 1, cell.getY(), 3, cell.getHeight() - 14));
        }

        g.setColour (! inLoop ? edge : (here ? accent : (step.rest ? muted : ink)));
        g.setFont (mono (step.rest ? 16.0f : 17.0f, ! step.rest));
        g.drawText (step.rest ? String ("-")
                              : noteName (dsp::acidMidiFor (step.degree, keyRoot, scale, octave)),
                    cell.withTrimmedTop (10), Justification::centred);
    }

    body.removeFromTop (16);

    // The four knobs the sticks turn: one row per stick, up-and-down first.
    // Handles, because they hold their value like the level bar does.
    auto knob = [&g] (Rectangle<int> cell, const String& label, float value)
    {
        g.setColour (muted);
        g.setFont (mono (10.5f, true));
        g.drawText (label, cell.removeFromLeft (76), Justification::centredLeft);

        g.setColour (ink);
        g.setFont (mono (12.0f, true));
        g.drawText (String (roundToInt (value * 100.0f)) + " %", cell.removeFromRight (44),
                    Justification::centredRight);

        const auto track = cell.reduced (4, 8);

        g.setColour (hair);
        g.fillRect (track);

        const auto filled = track.withWidth (roundToInt ((float) track.getWidth()
                                                         * jlimit (0.0f, 1.0f, value)));
        g.setColour (accent);
        g.fillRect (filled);

        g.setColour (edge);
        g.drawRect (track, 1);

        g.setColour (ink);
        g.fillRect (Rectangle<int> (filled.getRight() - 2, track.getY() - 3, 4,
                                    track.getHeight() + 6));
    };

    auto stickRow = [&g, &knob] (Rectangle<int> row, const String& stick,
                                 const String& upName, float upValue,
                                 const String& acrossName, float acrossValue)
    {
        g.setColour (muted);
        g.setFont (mono (10.0f, true));
        g.drawText (stick, row.removeFromLeft (56), Justification::centredLeft);

        auto first = row.removeFromLeft ((row.getWidth() - 12) / 2);
        row.removeFromLeft (12);

        knob (first, upName, upValue);
        knob (row, acrossName, acrossValue);
    };

    stickRow (body.removeFromTop (24), "V SPAK",
              "Y ENV", p.acidEnvMod.load (std::memory_order_relaxed),
              "X DECAY", p.acidDecay.load (std::memory_order_relaxed));
    body.removeFromTop (4);
    stickRow (body.removeFromTop (24), "H SPAK",
              "Y MIX", p.acidDelayMix.load (std::memory_order_relaxed),
              "X FEEDBACK", p.acidDelayFeedback.load (std::memory_order_relaxed));

    // What every button without a place of its own does — in the layer that is
    // up, so the text changes the moment LB goes down.
    auto legend = body.removeFromBottom (32);

    g.setColour (shift ? accent : muted);
    g.setFont (mono (10.0f, shift));

    if (shift)
    {
        g.drawText (String ("LB +  D-PAD UPP/NER TONART   VANSTER/HOGER OKTAV ")
                      + (octave > 0 ? "+" : "") + String (octave),
                    legend.removeFromTop (16), Justification::centredLeft);
        g.drawText ("LB +  RS RENSA MONSTRET     SLAPP LB FOR ATT SKRIVA STEG",
                    legend, Justification::centredLeft);
    }
    else
    {
        g.drawText (String ("D-PAD  UPP/NER TON  VANSTER/HOGER STEG     RS  SLUMPA     LS  VAG ")
                      + (square ? "FYRKANT" : "SAG"),
                    legend.removeFromTop (16), Justification::centredLeft);
        g.drawText ("HALL LB:  TONART  OKTAV  SKALA  GLIDE  LOOPSLUT  DELAYTAKT  RENSA",
                    legend, Justification::centredLeft);
    }
}

void InstrumentPanel::drawAcidLevels (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "NIVAER");

    const auto& p = processor.published();

    auto bar = [&g] (Rectangle<int> row, float value, const String& label,
                     const String& reading, bool interactive)
    {
        g.setColour (interactive ? ink : muted);
        g.setFont (mono (10.5f, true));
        g.drawText (label, row.removeFromLeft (kBarLabel), Justification::centredLeft);

        g.setColour (ink);
        g.setFont (mono (12.0f, true));
        g.drawText (reading, row.removeFromRight (kBarReading), Justification::centredRight);

        const auto track = row.reduced (6, 7);

        g.setColour (hair);
        g.fillRect (track);

        const auto filled = track.withWidth (roundToInt ((float) track.getWidth()
                                                         * jlimit (0.0f, 1.0f, value)));
        g.setColour (accent);
        g.fillRect (filled);

        g.setColour (interactive ? muted : edge);
        g.drawRect (track, 1);

        if (interactive)
        {
            g.setColour (ink);
            g.fillRect (Rectangle<int> (filled.getRight() - 2, track.getY() - 3, 4,
                                        track.getHeight() + 6));
        }
    };

    const auto cutoff = p.acidCutoff.load (std::memory_order_relaxed);
    const auto reso   = p.acidResonance.load (std::memory_order_relaxed);

    bar (levelRow (area, 0), cutoff, "RT",
         "cutoff " + String (roundToInt (cutoff * 100.0f)) + " %", false);

    bar (levelRow (area, 1), reso, "LT",
         "reso " + String (roundToInt (reso * 100.0f)) + " %", false);

    const auto level = processor.acidLevel();

    bar (levelRow (area, 2), level, "NIVA", levelReading (level), true);
}

void InstrumentPanel::drawAcidReadout (Graphics& g, Rectangle<int> area)
{
    const auto& p = processor.published();

    // Each setting captioned with what changes it. These four live in the shift
    // layer, so the captions say LB+ until LB is down, and then drop it and
    // light up.
    const bool   shift = p.acidShift.load (std::memory_order_relaxed);
    const String lb    = shift ? "" : "LB+";

    const std::vector<std::pair<String, String>> cells
    {
        { lb + "A  STEG",  String (p.acidLength.load (std::memory_order_relaxed)) },
        { lb + "B  GLIDE", String (dsp::acidGlideName (p.acidGlideMode.load (std::memory_order_relaxed))) },
        { lb + "X  DELAY", String (dsp::acidDelayName (p.acidDelayMode.load (std::memory_order_relaxed))) },
        { lb + "Y  SKALA", String (music::scaleAt (p.acidScale.load (std::memory_order_relaxed)).name) },
        { p.tempoFromHost.load (std::memory_order_relaxed) ? "TEMPO VARD" : "TEMPO", String() }
    };

    g.setColour (panel);
    g.fillRect (area);
    g.setColour (hair);
    g.drawRect (area, 1);

    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto cell = readoutCell (area, (int) i, (int) cells.size());

        if (i > 0)
        {
            g.setColour (hair);
            g.drawLine ((float) cell.getX(), (float) cell.getY() + 10.0f,
                        (float) cell.getX(), (float) cell.getBottom() - 10.0f, 1.0f);
        }

        auto inner = cell.reduced (8, 14);

        g.setColour (shift && i < 4 ? accent : muted);
        g.setFont (mono (9.5f, true));
        g.drawText (cells[i].first, inner.removeFromTop (14), Justification::centred);

        // The tempo's number is the clickable field, not paint.
        if (i == 4)
            continue;

        g.setColour (i == 0 ? accent : ink);
        g.setFont (mono (i == 0 ? 24.0f : 18.0f, true));
        g.drawFittedText (cells[i].second, inner, Justification::centred, 1, 0.8f);
    }
}

//== arpeggiator =============================================================

bool InstrumentPanel::isArp() const
{
    return processor.instrument() == ChordtrollerAudioProcessor::Instrument::arp;
}

void InstrumentPanel::drawArpPlate (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "ARPEGGIO");

    const auto& p = processor.published();

    const int   mode     = p.arpMode.load (std::memory_order_relaxed);
    const int   rate     = p.arpRate.load (std::memory_order_relaxed);
    const bool  playing  = p.arpPlaying.load (std::memory_order_relaxed);
    const bool  hold     = p.arpHold.load (std::memory_order_relaxed);
    const bool  triplet  = p.arpTriplet.load (std::memory_order_relaxed);
    const bool  drums    = p.arpDrums.load (std::memory_order_relaxed);
    const bool  shift    = p.arpShift.load (std::memory_order_relaxed);
    const int   delay    = p.arpDelay.load (std::memory_order_relaxed);
    const int   rhythm   = p.arpRhythm.load (std::memory_order_relaxed);
    const int   position = p.arpRhythmPos.load (std::memory_order_relaxed);
    const int   count    = jlimit (0, 16, p.arpPoolSize.load (std::memory_order_relaxed));
    const int   last     = p.arpLastIndex.load (std::memory_order_relaxed);
    const int   winLo    = p.arpWindowLo.load (std::memory_order_relaxed);
    const int   winSize  = p.arpWindowSize.load (std::memory_order_relaxed);
    const float share    = p.arpGateShare.load (std::memory_order_relaxed);
    const int   reg      = p.arpRegister.load (std::memory_order_relaxed);
    const int   octaves  = p.arpOctaves.load (std::memory_order_relaxed);

    auto body = area.reduced (16).withTrimmedTop (18);

    // The buttons with no other place, named — in the layer that is up, so
    // the text changes the moment VIEW goes down.
    auto legend = body.removeFromBottom (34);
    body.removeFromBottom (6);

    g.setColour (shift ? accent : muted);
    g.setFont (mono (10.0f, shift));

    if (shift)
    {
        g.drawText ("VIEW +  D-PAD UPP/NER REGISTER " + String (reg > 0 ? "+" : "") + String (reg)
                      + "   SIDLED OKTAVER " + String (octaves),
                    legend.removeFromTop (16), Justification::centredLeft);
        g.drawText ("VIEW +  " + buttonName ("A") + " DELAY   " + buttonName ("X") + " TRIOL   "
                      + buttonName ("Y") + " DUR/MOLL     SLAPP VIEW FOR SATTEN",
                    legend, Justification::centredLeft);
    }
    else
    {
        g.drawText (buttonName ("H SPAK") + ": SIDLED OMFANG, UPP/NER TONLANGD   RS HALL   "
                      + buttonName ("MENY") + " TRUMMOR",
                    legend.removeFromTop (16), Justification::centredLeft);
        g.drawText ("HALL VIEW: REGISTER, OKTAVER, DELAY, TRIOL, DUR/MOLL",
                    legend, Justification::centredLeft);
    }

    // Top: the speed the trigger has chosen, then what is on.
    auto top = body.removeFromTop (30);

    g.setColour (playing ? accent : edge);
    g.setFont (mono (24.0f, true));
    g.drawText (playing ? String (dsp::arpRateName (rate, triplet)) : String ("STILLA"),
                top.removeFromLeft (110), Justification::centredLeft);

    g.setColour (muted);
    g.setFont (mono (11.0f));
    g.drawText ("ton " + String (roundToInt (share * 100.0f)) + " %",
                top.removeFromLeft (80), Justification::centredLeft);

    auto badges = top.withSizeKeepingCentre (top.getWidth(), 22);

    auto badge = [&g, &badges] (const String& text, bool on)
    {
        auto b = badges.removeFromRight (jmax (40, 10 + 7 * text.length()));
        badges.removeFromRight (4);

        g.setColour (on ? accent : hair);
        g.fillRect (b);
        g.setColour (on ? ground : muted);
        g.setFont (mono (9.5f, true));
        g.drawText (text, b, Justification::centred);
    };

    badge ("TRUMMOR", drums);
    badge (delay == 0 ? String ("DELAY AV") : delay == 1 ? String ("DELAY 1/8") : String ("DELAY 3/16"), delay > 0);
    badge ("TRIOL", triplet);
    badge ("HALL", hold);

    body.removeFromTop (8);

    // Bottom: the four buttons' patterns, and the rhythm.
    auto rhythmRow = body.removeFromBottom (24);
    body.removeFromBottom (8);
    auto modes = body.removeFromBottom (40);
    body.removeFromBottom (8);

    struct Pair { const char* button; int first, second; };
    static constexpr Pair pairs[4] { { "Y", 0, 5 }, { "B", 2, 1 }, { "X", 6, 7 }, { "A", 4, 3 } };

    for (int i = 0; i < 4; ++i)
    {
        auto cell = Rectangle<int> (modes.getX() + (i % 2) * (modes.getWidth() / 2),
                                    modes.getY() + (i / 2) * 20, modes.getWidth() / 2, 20);

        g.setColour (shift ? edge : muted);
        g.setFont (mono (10.0f, true));
        g.drawText (buttonName (pairs[i].button), cell.removeFromLeft (22), Justification::centredLeft);

        for (const int m : { pairs[i].first, pairs[i].second })
        {
            const bool on = (m == mode);
            auto slot = cell.removeFromLeft (cell.getWidth() / (m == pairs[i].first ? 2 : 1));

            g.setColour (on ? (playing ? accent : ink) : (shift ? edge : muted));
            g.setFont (mono (on ? 11.0f : 10.0f, on));
            g.drawText (String (dsp::arpModeName (m)), slot, Justification::centredLeft);
        }
    }

    g.setColour (muted);
    g.setFont (mono (9.5f, true));
    g.drawText (buttonName ("RB") + " " + String (dsp::arpRhythmName (rhythm)),
                rhythmRow.removeFromLeft (96), Justification::centredLeft);

    const float sw = (float) rhythmRow.getWidth() / 16.0f;

    for (int s = 0; s < 16; ++s)
    {
        const auto step = dsp::arpRhythmStep (rhythm, s);
        const float h   = step.accent ? 20.0f : 12.0f;
        const auto cell = Rectangle<float> ((float) rhythmRow.getX() + sw * (float) s + 1.5f,
                                            (float) rhythmRow.getBottom() - h, sw - 3.0f, h);

        g.setColour (step.hit ? edge : hair);
        g.fillRect (cell);

        if (playing && s == position)
        {
            g.setColour (accent);
            g.drawRect (cell.expanded (1.0f), 2.0f);
        }
    }

    // Middle: the plate. Every chord tone in the span as a string, low to
    // high; the window the right stick leaves open is drawn, the rest dimmed,
    // and the note just struck is lit — the Omnichord's zone, made visible.
    if (count > 0)
    {
        const float w = (float) body.getWidth() / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const int  note   = p.arpPool[i].load (std::memory_order_relaxed);
            const bool inZone = i >= winLo && i < winLo + winSize;
            const bool lit    = playing && i == last;
            const float x     = (float) body.getX() + w * ((float) i + 0.5f);

            g.setColour (lit ? accent : (inZone ? muted : hair));
            g.drawLine (x, (float) body.getY(), x, (float) body.getBottom() - 16.0f,
                        lit ? 3.5f : (inZone ? 2.0f : 1.0f));

            if (w >= 22.0f)
            {
                g.setColour (lit ? accent : (inZone ? ink : edge));
                g.setFont (mono (9.0f, lit));
                g.drawText (noteName (note), Rectangle<float> (x - w * 0.5f, (float) body.getBottom() - 14.0f, w, 14.0f),
                            Justification::centred);
            }
        }
    }
}

void InstrumentPanel::drawArpLevels (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "NIVAER");

    const auto& p = processor.published();

    auto bar = [&g] (Rectangle<int> row, float value, const String& label,
                     const String& reading, bool interactive)
    {
        g.setColour (interactive ? ink : muted);
        g.setFont (mono (10.5f, true));
        g.drawText (label, row.removeFromLeft (kBarLabel), Justification::centredLeft);

        g.setColour (ink);
        g.setFont (mono (12.0f, true));
        g.drawText (reading, row.removeFromRight (kBarReading), Justification::centredRight);

        const auto track = row.reduced (6, 7);

        g.setColour (hair);
        g.fillRect (track);

        const auto filled = track.withWidth (roundToInt ((float) track.getWidth() * jlimit (0.0f, 1.0f, value)));
        g.setColour (accent);
        g.fillRect (filled);

        g.setColour (interactive ? muted : edge);
        g.drawRect (track, 1);

        if (interactive)
        {
            g.setColour (ink);
            g.fillRect (Rectangle<int> (filled.getRight() - 2, track.getY() - 3, 4, track.getHeight() + 6));
        }
    };

    // The speed's three steps marked on its bar, where the trigger crosses
    // from one to the next.
    const auto speed   = p.arpSpeed.load (std::memory_order_relaxed);
    const bool playing = p.arpPlaying.load (std::memory_order_relaxed);
    const auto rtRow   = levelRow (area, 0);

    bar (rtRow, speed, buttonName ("RT"),
         playing ? "fart " + String (dsp::arpRateName (p.arpRate.load (std::memory_order_relaxed),
                                                        p.arpTriplet.load (std::memory_order_relaxed)))
                 : String ("stilla"), false);

    const auto track = barTrack (rtRow);
    g.setColour (ink.withAlpha (0.5f));

    for (const float mark : { 0.06f, 0.45f, 0.85f })
    {
        const float x = (float) track.getX() + mark * (float) track.getWidth();
        g.drawLine (x, (float) track.getY() - 3.0f, x, (float) track.getBottom() + 3.0f, 1.0f);
    }

    const auto cutoff = p.arpCutoff.load (std::memory_order_relaxed);

    bar (levelRow (area, 1), cutoff, buttonName ("LT"),
         "cutoff " + String (roundToInt (cutoff * 100.0f)) + " %", false);

    const auto level = processor.arpLevel();

    bar (levelRow (area, 2), level, "NIVA", levelReading (level), true);
}

void InstrumentPanel::drawArpReadout (Graphics& g, Rectangle<int> area)
{
    const auto& p = processor.published();

    const std::vector<std::pair<String, String>> cells
    {
        { buttonName ("D-PAD") + " SIDLED  LJUD", String (dsp::arpSound (p.arpSound.load (std::memory_order_relaxed)).name) },
        { buttonName ("D-PAD") + " UPP/NER  TONART", String (ring.keyName()) },
        { buttonName ("Y") + " " + buttonName ("A") + " " + buttonName ("B") + " " + buttonName ("X") + "  SATT",
          String (dsp::arpModeName (p.arpMode.load (std::memory_order_relaxed))) },
        { buttonName ("RB") + "  RYTM", String (dsp::arpRhythmName (p.arpRhythm.load (std::memory_order_relaxed)))
                                          + (p.arpTriplet.load (std::memory_order_relaxed) ? " T" : "") },
        { p.tempoFromHost.load (std::memory_order_relaxed) ? "TEMPO VARD" : "TEMPO", String() }
    };

    g.setColour (panel);
    g.fillRect (area);
    g.setColour (hair);
    g.drawRect (area, 1);

    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto cell = readoutCell (area, (int) i, (int) cells.size());

        if (i > 0)
        {
            g.setColour (hair);
            g.drawLine ((float) cell.getX(), (float) cell.getY() + 10.0f,
                        (float) cell.getX(), (float) cell.getBottom() - 10.0f, 1.0f);
        }

        auto inner = cell.reduced (8, 14);

        g.setColour (muted);
        g.setFont (mono (9.5f, true));
        g.drawText (cells[i].first, inner.removeFromTop (14), Justification::centred);

        // The tempo's number is the clickable field, not paint.
        if (i == 4)
            continue;

        g.setColour (i == 0 ? accent : ink);
        g.setFont (mono (i == 0 ? 24.0f : 18.0f, true));
        g.drawFittedText (cells[i].second, inner, Justification::centred, 1, 0.8f);
    }
}

//== strings =================================================================

bool InstrumentPanel::isStrings() const
{
    return processor.instrument() == ChordtrollerAudioProcessor::Instrument::strings;
}

void InstrumentPanel::drawStringRing (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "TONRING");

    const auto& p = processor.published();

    const int sector     = p.stringSector.load (std::memory_order_relaxed);
    const int keyRoot    = p.keyRoot.load (std::memory_order_relaxed);
    const int instrument = p.stringInstrument.load (std::memory_order_relaxed);
    const int reg        = p.stringRegister.load (std::memory_order_relaxed);
    const int scale      = p.stringScale.load (std::memory_order_relaxed);
    const int octave     = p.stringOctave.load (std::memory_order_relaxed);
    const int drone      = p.stringDrone.load (std::memory_order_relaxed);

    auto body = area.reduced (16).withTrimmedTop (16);

    g.setColour (muted);
    g.setFont (mono (9.5f, true));
    g.drawText ("V SPAK TON   " + buttonName ("LB") + "/" + buttonName ("RB") + " OKTAV   LS BORDUN",
                body.removeFromBottom (14), Justification::centred);

    const auto cx = (float) body.getCentreX();
    const auto cy = (float) body.getCentreY();

    constexpr float labelGap  = 24.0f;
    constexpr float labelHalf = 30.0f;

    const auto r = jmax (56.0f, jmin (body.getWidth(), body.getHeight()) * 0.5f - labelGap - labelHalf);

    auto pointAt = [cx, cy] (int index, float radius)
    {
        const float rad = (float) index * 45.0f * pad::kPi / 180.0f;
        return Point<float> (cx + std::sin (rad) * radius, cy - std::cos (rad) * radius);
    };

    g.setColour (hair);
    g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);

    // The thumb's aim, while it is out far enough to choose.
    const auto& t = processor.tuning();
    const auto dz = pad::applyDeadzone (pad.lx, pad.ly, t.stickDeadzone);
    const auto sq = pad::discToSquare (dz.x, dz.y);

    if (std::sqrt (sq.x * sq.x + sq.y * sq.y) >= t.chordHoldRadius)
    {
        g.setColour (accent.withAlpha (0.35f));
        g.drawLine (cx, cy, cx + sq.x * r * 0.86f, cy - sq.y * r * 0.86f, 2.0f);
    }

    // Each place named with the note it stops, octave included, so LB and RB
    // are seen moving the whole ring.
    for (int i = 0; i < pad::kSectors; ++i)
    {
        const bool active = (i == sector);
        const auto dot = pointAt (i, r);

        g.setColour (active ? accent : edge);

        if (active)
            g.fillEllipse (dot.x - 8.0f, dot.y - 8.0f, 16.0f, 16.0f);
        else
            g.drawEllipse (dot.x - 4.5f, dot.y - 4.5f, 9.0f, 9.0f, 1.5f);

        const auto label = pointAt (i, r + labelGap);
        const int  note  = dsp::stringNoteFor (instrument, keyRoot, reg, scale, i, octave);

        g.setColour (active ? accent : muted);
        g.setFont (mono (active ? 15.0f : 13.0f, active));
        g.drawText (noteNameIn (note, keyRoot, scale),
                    Rectangle<float> (label.x - labelHalf, label.y - 10.0f, labelHalf * 2.0f, 20.0f),
                    Justification::centred);
    }

    // The drone is a mode that sounds, so it gets the same loud badge a latch
    // does. Otherwise the middle says which scale and which octave.
    if (drone >= 0)
    {
        auto badge = Rectangle<float> (cx - 54.0f, cy - 12.0f, 108.0f, 24.0f);

        g.setColour (accent);
        g.fillRect (badge);
        g.setColour (ground);
        g.setFont (mono (12.0f, true));
        g.drawText ("BORDUN " + noteNameIn (drone, keyRoot, scale), badge, Justification::centred);
    }
    else
    {
        g.setColour (octave != 0 ? accent : muted);
        g.setFont (mono (10.0f, octave != 0));
        g.drawText (octave > 0 ? String ("OKTAV UPP") : octave < 0 ? String ("OKTAV NER")
                                                                   : String (music::scaleAt (scale).name),
                    Rectangle<float> (cx - 60.0f, cy - 9.0f, 120.0f, 18.0f), Justification::centred);
    }
}

void InstrumentPanel::drawStringBow (Graphics& g, Rectangle<int> area)
{
    const auto& p = processor.published();

    const bool  pizz     = p.stringPizz.load (std::memory_order_relaxed);
    const bool  section  = p.stringSection.load (std::memory_order_relaxed);
    const bool  sordino  = p.stringSordino.load (std::memory_order_relaxed);
    const float note     = p.stringNote.load (std::memory_order_relaxed);
    const int   open     = p.stringOpen.load (std::memory_order_relaxed);
    const float bow      = p.stringBow.load (std::memory_order_relaxed);
    const float vibrato  = p.stringVibrato.load (std::memory_order_relaxed);
    const float tremolo  = p.stringTremolo.load (std::memory_order_relaxed);
    const float level    = p.stringLevel.load (std::memory_order_relaxed);
    const float beta     = p.stringBowPoint.load (std::memory_order_relaxed);
    const int   reg      = p.stringRegister.load (std::memory_order_relaxed);
    const int   keyRoot  = p.keyRoot.load (std::memory_order_relaxed);
    const int   scale    = p.stringScale.load (std::memory_order_relaxed);
    const bool  sharps   = music::spellWithSharps (keyRoot, music::scaleAt (scale).mask);

    panelBox (g, area, pizz ? "PIZZICATO" : "STRAKEN");

    auto body = area.reduced (16).withTrimmedTop (18);
    auto head = body.removeFromTop (52);

    const bool sounding = level > 0.002f;

    g.setColour (sounding ? accent : edge);
    g.setFont (mono (40.0f, true));
    g.drawText (noteNameIn ((int) std::lround (note), keyRoot, scale), head, Justification::centredLeft);

    // The modes, as badges, only while they are on.
    auto badges = head.withSizeKeepingCentre (head.getWidth(), 22);

    auto badge = [&g, &badges] (const String& text)
    {
        auto b = badges.removeFromRight (jmax (70, 12 + 8 * text.length()));
        badges.removeFromRight (6);

        g.setColour (accent);
        g.fillRect (b);
        g.setColour (ground);
        g.setFont (mono (11.0f, true));
        g.drawText (text, b, Justification::centred);
    };

    if (pizz)    badge ("PIZZ");
    if (sordino) badge ("SORDIN");
    if (section) badge ("SEKTION");

    // Claimed first, so the string gets whatever height is left over rather
    // than a fixed strip with a hole under it.
    auto legend = body.removeFromBottom (34);
    body.removeFromBottom (10);
    auto handRow = body.removeFromBottom (18);
    body.removeFromBottom (10);
    body.removeFromTop (6);

    // The string itself: nut on the left, bridge on the right, the finger where
    // the note puts it, and the bow its share of the sounding length from the
    // bridge. The part that sounds swells with the level.
    auto board = body.reduced (4, 0);

    const float nutX    = (float) board.getX() + 40.0f;
    const float bridgeX = (float) board.getRight() - 16.0f;
    const float midY    = (float) board.getCentreY();

    const float stopped = jlimit (0.0f, 1.0f, std::exp2 (-(note - (float) open) / 12.0f));
    const float fingerX = nutX + (1.0f - stopped) * (bridgeX - nutX);
    const float bowX    = bridgeX - beta * (bridgeX - fingerX);

    g.setColour (muted);
    g.setFont (mono (10.0f, true));
    g.drawText (String (music::pitchClassNameSpelt (open % 12, sharps)),
                Rectangle<float> ((float) board.getX(), midY - 8.0f, 30.0f, 16.0f), Justification::centredRight);

    g.setColour (edge);
    g.fillRect (Rectangle<float> (nutX - 3.0f, midY - 12.0f, 3.0f, 24.0f));

    Path bridge;
    bridge.addTriangle (bridgeX, midY - 2.0f, bridgeX - 7.0f, midY + 14.0f, bridgeX + 7.0f, midY + 14.0f);
    g.fillPath (bridge);

    // Behind the finger the string is silent.
    g.setColour (hair);
    g.drawLine (nutX, midY, fingerX, midY, 1.5f);

    const float swing = jlimit (0.0f, 1.0f, level * 5.0f) * (float) board.getHeight() * 0.18f;

    Path lens;
    lens.startNewSubPath (fingerX, midY);
    lens.quadraticTo ((fingerX + bridgeX) * 0.5f, midY - 2.0f * swing, bridgeX, midY);
    lens.quadraticTo ((fingerX + bridgeX) * 0.5f, midY + 2.0f * swing, fingerX, midY);
    lens.closeSubPath();

    g.setColour (accent.withAlpha (sounding ? 0.22f : 0.0f));
    g.fillPath (lens);
    g.setColour (sounding ? accent : edge);
    g.strokePath (lens, PathStrokeType (1.5f));

    g.setColour (accent);
    g.fillEllipse (fingerX - 6.0f, midY - 6.0f, 12.0f, 12.0f);

    // The bow, across the string, with the way it is travelling.
    if (! pizz)
    {
        const bool moving = std::abs (bow) > 0.001f;
        const auto hairs  = Rectangle<float> (bowX - 3.0f, midY - 40.0f, 6.0f, 80.0f);

        g.setColour (moving ? ink : edge);
        g.fillRect (hairs);

        if (moving)
        {
            Path arrow;
            const float tip  = bow > 0.0f ? hairs.getY() - 2.0f : hairs.getBottom() + 2.0f;
            const float base = bow > 0.0f ? tip + 9.0f : tip - 9.0f;
            arrow.addTriangle (bowX, tip, bowX - 7.0f, base, bowX + 7.0f, base);
            g.setColour (accent);
            g.fillPath (arrow);
        }
    }

    // What the right hand is doing, in words.
    g.setColour (ink);
    g.setFont (mono (13.0f, true));

    String hand;

    if (pizz)
        hand = "SNARTA " + buttonName ("H SPAK") + " UPP ELLER NER FOR ATT KNAPPA";
    else if (std::abs (bow) < 0.001f)
        hand = "STRAKEN VILAR";
    else
        hand = String (bow > 0.0f ? "UPPSTRAK " : "NEDSTRAK ") + String (roundToInt (std::abs (bow) * 100.0f)) + " %";

    if (vibrato > 0.0f)
        hand << "   VIBRATO " << roundToInt (vibrato * 100.0f) << " %";
    else if (tremolo > 0.0f)
        hand << "   TREMOLO " << roundToInt (tremolo * 100.0f) << " %";

    g.drawText (hand, handRow, Justification::centredLeft);

    // The buttons with no other place on this screen, named.
    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText (buttonName ("H SPAK") + (pizz ? " UPP/NER KNAPPA" : " UPP/NER STRAKE")
                  + "   HOGER VIBRATO   VANSTER TREMOLO",
                legend.removeFromTop (16), Justification::centredLeft);
    g.drawText ("RS " + String (pizz ? "ARCO" : "PIZZ") + "   " + buttonName ("Y") + " SORDIN   "
                  + buttonName ("MENY") + " SEKTION   " + buttonName ("VIEW") + " REGISTER "
                  + (reg > 0 ? "+" : "") + String (reg),
                legend, Justification::centredLeft);
}

void InstrumentPanel::drawStringLevels (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "NIVAER");

    const auto& p = processor.published();

    auto bar = [&g] (Rectangle<int> row, float value, const String& label,
                     const String& reading, bool interactive)
    {
        g.setColour (interactive ? ink : muted);
        g.setFont (mono (10.5f, true));
        g.drawText (label, row.removeFromLeft (kBarLabel), Justification::centredLeft);

        g.setColour (ink);
        g.setFont (mono (12.0f, true));
        g.drawText (reading, row.removeFromRight (kBarReading), Justification::centredRight);

        const auto track = row.reduced (6, 7);

        g.setColour (hair);
        g.fillRect (track);

        const auto filled = track.withWidth (roundToInt ((float) track.getWidth() * jlimit (0.0f, 1.0f, value)));
        g.setColour (accent);
        g.fillRect (filled);

        g.setColour (interactive ? muted : edge);
        g.drawRect (track, 1);

        if (interactive)
        {
            g.setColour (ink);
            g.fillRect (Rectangle<int> (filled.getRight() - 2, track.getY() - 3, 4, track.getHeight() + 6));
        }
    };

    // The pressure's three regions sit where the engine put them: the first
    // quarter of the trigger is the surface sound, the middle half the tone,
    // the last quarter the crunch. Marked on the bar, so the finger learns
    // where the tone is.
    const auto pressure = p.stringPressure.load (std::memory_order_relaxed);
    const auto rtRow    = levelRow (area, 0);

    // In pizzicato there is no bow to lean on, and the bar says so rather
    // than reporting a pressure nothing is using.
    const bool pizz = p.stringPizz.load (std::memory_order_relaxed);

    bar (rtRow, pizz ? 0.0f : pressure, buttonName ("RT"),
         pizz ? String ("av i pizz")
              : String (pressure < 0.25f ? "ytljud " : pressure < 0.75f ? "full ton " : "skorr ")
                  + String (roundToInt (pressure * 100.0f)) + " %", false);

    const auto track = barTrack (rtRow);

    g.setColour (ink.withAlpha (0.5f));

    for (const float mark : { 0.25f, 0.75f })
    {
        const float x = (float) track.getX() + mark * (float) track.getWidth();
        g.drawLine (x, (float) track.getY() - 3.0f, x, (float) track.getBottom() + 3.0f, 1.0f);
    }

    const auto glide = p.stringGlide.load (std::memory_order_relaxed);

    bar (levelRow (area, 1), glide, buttonName ("LT"),
         glide < 0.02f ? String ("fingret hoppar")
                       : "glid " + String (roundToInt (p.stringGlideSeconds.load (std::memory_order_relaxed) * 1000.0f)) + " ms",
         false);

    const auto level = processor.stringLevel();

    bar (levelRow (area, 2), level, "NIVA", levelReading (level), true);
}

void InstrumentPanel::drawStringReadout (Graphics& g, Rectangle<int> area)
{
    const auto& p = processor.published();

    const std::vector<std::pair<String, String>> cells
    {
        { buttonName ("V SPAK") + "  TON", noteNameIn ((int) std::lround (p.stringNote.load (std::memory_order_relaxed)),
                                                       p.keyRoot.load (std::memory_order_relaxed),
                                                       p.stringScale.load (std::memory_order_relaxed)) },
        { buttonName ("D-PAD") + " UPP/NER  TONART", String (music::pitchClassName (p.keyRoot.load (std::memory_order_relaxed))) },
        { buttonName ("A") + " " + buttonName ("B") + " " + buttonName ("X") + "  SKALA",
          String (music::scaleAt (p.stringScale.load (std::memory_order_relaxed)).name) },
        { buttonName ("D-PAD") + " SIDLED  INSTRUMENT",
          String (dsp::stringInstrument (p.stringInstrument.load (std::memory_order_relaxed)).name) },
        { buttonName ("MENY") + "  SPELARE", p.stringSection.load (std::memory_order_relaxed) ? String ("SEKTION") : String ("SOLO") }
    };

    g.setColour (panel);
    g.fillRect (area);
    g.setColour (hair);
    g.drawRect (area, 1);

    for (size_t i = 0; i < cells.size(); ++i)
    {
        auto cell = readoutCell (area, (int) i, (int) cells.size());

        if (i > 0)
        {
            g.setColour (hair);
            g.drawLine ((float) cell.getX(), (float) cell.getY() + 10.0f,
                        (float) cell.getX(), (float) cell.getBottom() - 10.0f, 1.0f);
        }

        auto inner = cell.reduced (8, 14);

        g.setColour (muted);
        g.setFont (mono (9.5f, true));
        g.drawText (cells[i].first, inner.removeFromTop (14), Justification::centred);

        g.setColour (i == 0 ? accent : ink);
        g.setFont (mono (i == 0 ? 28.0f : 18.0f, true));
        g.drawFittedText (cells[i].second, inner, Justification::centred, 1, 0.8f);
    }
}

} // namespace chordtroller
