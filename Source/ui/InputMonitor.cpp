#include "InputMonitor.h"
#include "../PluginProcessor.h"
#include "../dsp/CompEngine.h"

using namespace juce;

namespace chordtroller
{

namespace
{
    // The plan document's Modernist palette, so the diagnostic screen and the
    // paperwork are visibly the same project. This is not the plugin's face —
    // that arrives in phase 4 from the design handoff.
    const Colour ground { 0xff17120f };
    const Colour panel  { 0xff1e1815 };
    const Colour ink    { 0xfff2ede9 };
    const Colour muted  { 0xffa49c96 };
    const Colour hair   { 0xff332b27 };
    const Colour edge   { 0xff4a413c };
    const Colour accent { 0xffff5c3d };
    const Colour warn   { 0xffffc043 };

    Font mono (float size, bool bold = false)
    {
        return Font (FontOptions (Font::getDefaultMonospacedFontName(), size,
                                  bold ? Font::bold : Font::plain));
    }

    struct ButtonLabel { PadButton bit; const char* name; };

    const ButtonLabel kButtons[] =
    {
        { padA, "A" },            { padB, "B" },            { padX, "X" },
        { padY, "Y" },            { padLB, "LB" },          { padRB, "RB" },
        { padView, "VIEW" },      { padMenu, "MENY" },      { padLS, "LS" },
        { padRS, "RS" },          { padDpadUp, "D-UPP" },   { padDpadDown, "D-NER" },
        { padDpadLeft, "D-VAN" }, { padDpadRight, "D-HOG" }
    };

    constexpr int kNumButtons = (int) (sizeof (kButtons) / sizeof (kButtons[0]));

    int slotFor (PadButton bit) noexcept
    {
        for (int i = 0; i < ChordtrollerAudioProcessor::Published::kButtonSlots; ++i)
            if ((std::uint16_t) bit == (std::uint16_t) (1u << i))
                return i;

        return -1;
    }

    String num (float v, int decimals = 3)
    {
        return String (v, decimals).paddedLeft (' ', decimals + 3);
    }

    /** MIDI 60 is C4 here, the scientific convention, so C3 reads as the 48 the
        plate table is written in. */
    String noteName (int midiNote)
    {
        if (midiNote < 0)
            return "-";

        return String (music::pitchClassName (midiNote % 12))
                 + String (midiNote / 12 - 1);
    }

    void panelBox (Graphics& g, Rectangle<int> area, const String& title)
    {
        g.setColour (panel);
        g.fillRect (area);
        g.setColour (hair);
        g.drawRect (area, 1);

        g.setColour (muted);
        g.setFont (mono (10.5f, true));
        g.drawText (title, area.reduced (10, 6).removeFromTop (14),
                    Justification::centredLeft);
    }

    /** Label left, value right. Right-aligned because these are read as a column
        of changing numbers, and a ragged right edge makes a moving digit
        impossible to follow. */
    void valueRows (Graphics& g, Rectangle<int> area,
                    const std::vector<std::pair<String, String>>& rows,
                    float size = 12.0f)
    {
        for (const auto& row : rows)
        {
            auto line = area.removeFromTop ((int) (size * 1.65f));

            g.setColour (muted);
            g.setFont (mono (size));
            g.drawText (row.first, line, Justification::centredLeft);

            g.setColour (ink);
            g.setFont (mono (size, true));
            g.drawText (row.second, line, Justification::centredRight);
        }
    }

    /**
        One stick: the round gate it physically sits in, the square that gate is
        mapped onto, the deadzone it has to clear, and both dots.

        Screen y grows downwards and the pad's y grows upwards, so every plot
        here flips. Getting that wrong is invisible in the numbers and instantly
        obvious in the picture, which is most of why the picture is here.
    */
    void stickPlot (Graphics& g, Rectangle<int> area,
                    float rawX, float rawY, float sqX, float sqY,
                    float deadzone, int activeSector, bool showSectors)
    {
        const auto box = area.toFloat().reduced (2.0f);
        const auto cx = box.getCentreX();
        const auto cy = box.getCentreY();
        const auto r  = jmin (box.getWidth(), box.getHeight()) * 0.5f;

        auto toScreen = [cx, cy, r] (float x, float y)
        {
            return Point<float> (cx + x * r, cy - y * r);
        };

        g.setColour (hair);
        g.drawRect (Rectangle<float> (cx - r, cy - r, r * 2.0f, r * 2.0f), 1.0f);

        g.setColour (edge);
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);

        g.setColour (hair);
        g.drawLine (cx - r, cy, cx + r, cy, 1.0f);
        g.drawLine (cx, cy - r, cx, cy + r, 1.0f);

        if (showSectors)
        {
            // Boundaries sit 22.5 degrees off each compass point, so the tonic is
            // centred on straight up rather than straddling it.
            for (int i = 0; i < pad::kSectors; ++i)
            {
                const float rad = ((float) i * 45.0f + 22.5f) * pad::kPi / 180.0f;
                const auto  p   = toScreen (std::sin (rad), std::cos (rad));

                g.setColour (hair);
                g.drawLine (cx, cy, p.x, p.y, 1.0f);
            }

            if (activeSector >= 0)
            {
                const float rad = (float) activeSector * 45.0f * pad::kPi / 180.0f;
                const auto  p   = toScreen (std::sin (rad) * 0.92f, std::cos (rad) * 0.92f);

                g.setColour (accent.withAlpha (0.85f));
                g.drawLine (cx, cy, p.x, p.y, 2.0f);
            }
        }

        if (deadzone > 0.0f)
        {
            const float dr = r * deadzone;
            g.setColour (edge);
            g.drawEllipse (cx - dr, cy - dr, dr * 2.0f, dr * 2.0f, 1.0f);
        }

        const auto rawPoint = toScreen (rawX, rawY);
        g.setColour (muted.withAlpha (0.55f));
        g.fillEllipse (rawPoint.x - 4.0f, rawPoint.y - 4.0f, 8.0f, 8.0f);

        const auto sqPoint = toScreen (sqX, sqY);
        g.setColour (accent);
        g.drawLine (rawPoint.x, rawPoint.y, sqPoint.x, sqPoint.y, 1.0f);
        g.fillEllipse (sqPoint.x - 5.0f, sqPoint.y - 5.0f, 10.0f, 10.0f);
    }

    void meterBar (Graphics& g, Rectangle<int> area, float value01,
                   const String& label, const String& reading, Colour fill)
    {
        auto row = area;

        g.setColour (muted);
        g.setFont (mono (11.0f, true));
        g.drawText (label, row.removeFromLeft (34), Justification::centredLeft);

        g.setColour (ink);
        g.setFont (mono (12.0f, true));
        g.drawText (reading, row.removeFromRight (118), Justification::centredRight);

        auto bar = row.reduced (4, 6);
        g.setColour (hair);
        g.fillRect (bar);

        g.setColour (fill);
        g.fillRect (bar.withWidth (roundToInt ((float) bar.getWidth()
                                               * jlimit (0.0f, 1.0f, value01))));

        g.setColour (edge);
        g.drawRect (bar, 1);
    }

    /** The plate, in plate order — which is not pitch order. The string just
        plucked is lit, and anything the octave zone has shut out is dimmed, so
        the window the right thumb is choosing is visible rather than inferred. */
    void stringRow (Graphics& g, Rectangle<int> area, int count, int index,
                    int zoneLow, int zoneHigh)
    {
        if (count <= 0)
            return;

        const float w = (float) area.getWidth() / (float) count;

        for (int i = 0; i < count; ++i)
        {
            const auto x = (float) area.getX() + w * ((float) i + 0.5f);
            const bool lit    = (i == index);
            const bool inZone = (i >= zoneLow && i <= zoneHigh);

            g.setColour (lit ? accent : (inZone ? edge : hair));
            g.drawLine (x, (float) area.getY(), x, (float) area.getBottom(),
                        lit ? 3.0f : (inZone ? 1.5f : 1.0f));
        }
    }
}

//==============================================================================

InputMonitor::InputMonitor (ChordtrollerAudioProcessor& p)
    : processor (p)
{
    resetButton.setColour (TextButton::buttonColourId, panel);
    resetButton.setColour (TextButton::textColourOffId, muted);
    resetButton.onClick = [this]
    {
        processor.gamepad().resetTiming();
        processor.clearCounters();
    };
    addAndMakeVisible (resetButton);

    // 60 Hz is the screen's business. The reader is still at 250 and the audio
    // thread reads it per block.
    startTimerHz (60);
}

void InputMonitor::timerCallback()
{
    pad = processor.gamepad().state();

    // Retuned to whatever the audio thread is actually in, so names can be
    // composed here without sending text across a thread.
    ring.setKeyRoot (processor.published().keyRoot.load (std::memory_order_relaxed));
    ring.setBank (processor.published().bank.load (std::memory_order_relaxed));
    ring.setMode (processor.published().minorMode.load (std::memory_order_relaxed)
                    ? music::Mode::minor : music::Mode::major);

    repaint();
}

void InputMonitor::resized()
{
    // Left of the editor's own toggle, which sits in the same corner.
    resetButton.setBounds (getWidth() - 252, 18, 110, 24);
}

void InputMonitor::paint (Graphics& g)
{
    g.fillAll (ground);

    auto area = getLocalBounds().reduced (20);

    drawHeader (g, area.removeFromTop (56));
    area.removeFromTop (14);

    auto sticks = area.removeFromTop (296);
    drawLeftStick  (g, sticks.removeFromLeft (sticks.getWidth() / 2 - 7));
    sticks.removeFromLeft (14);
    drawRightStick (g, sticks);

    area.removeFromTop (14);

    auto middle = area.removeFromTop (130);
    drawTriggers   (g, middle.removeFromLeft (middle.getWidth() / 2 - 7));
    middle.removeFromLeft (14);
    drawInstrument (g, middle);

    area.removeFromTop (14);
    drawButtons (g, area);
}

void InputMonitor::drawHeader (Graphics& g, Rectangle<int> area)
{
    auto& reader = processor.gamepad();

    g.setColour (accent);
    g.fillRect (area.removeFromBottom (2));

    auto left = area.removeFromLeft (area.getWidth() / 2);

    g.setColour (ink);
    g.setFont (mono (22.0f, true));
    g.drawText ("CHORDTROLLER", left.removeFromTop (28), Justification::bottomLeft);

    g.setColour (muted);
    g.setFont (mono (10.5f));
    g.drawText ("FAS 2 - HALL IN RT FOR ATT HORA NAGOT", left, Justification::topLeft);

    const auto hz    = reader.measuredHz();
    const auto worst = reader.worstIntervalMs();

    auto right = area.withTrimmedRight (250);

    g.setFont (mono (11.0f, true));
    g.setColour (pad.connected ? accent : muted);
    g.drawText (pad.connected ? "KONTROLL ANSLUTEN" : "INGEN KONTROLL",
                right.removeFromTop (18), Justification::centredRight);

    g.setFont (mono (10.5f));
    g.setColour (muted);
    g.drawText (String (reader.sourceName())
                  + (reader.usingHighResolutionTimer() ? "  hogupplost timer"
                                                       : "  GROV TIMER"),
                right.removeFromTop (16), Justification::centredRight);

    // 200 Hz is where a 4 ms loop has clearly stopped being one, and 12 ms is
    // where a starting pluck begins to feel late.
    g.setColour (hz >= 200.0f && worst < 12.0f ? ink : warn);
    g.setFont (mono (11.0f, true));
    g.drawText (String (hz, 0) + " Hz   varst " + String (worst, 1) + " ms",
                right.removeFromTop (16), Justification::centredRight);
}

void InputMonitor::drawLeftStick (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "VANSTER SPAK - ACKORD");

    const auto& t = processor.tuning();
    const auto dz = pad::applyDeadzone (pad.lx, pad.ly, t.stickDeadzone);
    const auto sq = pad::discToSquare (dz.x, dz.y);

    const int sector = processor.published().sector.load (std::memory_order_relaxed);

    auto body = area.reduced (12).withTrimmedTop (18);
    auto plot = body.removeFromLeft (body.getHeight());
    body.removeFromLeft (14);

    stickPlot (g, plot, pad.lx, pad.ly, sq.x, sq.y,
               t.chordHoldRadius, sector, true);

    const auto& p = processor.published();

    valueRows (g, body,
    {
        { "ra x",      num (pad.lx) },
        { "ra y",      num (pad.ly) },
        { "kvadrat x", num (sq.x) },
        { "kvadrat y", num (sq.y) },
        { "baring",    String (pad::bearingDegrees (sq.x, sq.y), 1) + " gr" },
        { "sektor",    String (sector) + " " + pad::sectorName (sector)
                         + (p.chordLatched.load (std::memory_order_relaxed)
                              ? String ("  LAST") : String()) },
        { "tonart",    String (ring.keyName()) },
        { "bank",      String (ring.bankName()) },
        { "ackord",    String (ring.nameAt (sector)) }
    });
}

void InputMonitor::drawRightStick (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "HOGER SPAK - PLOCK OCH OKTAVZON");

    const auto& t = processor.tuning();
    const auto dz = pad::applyDeadzone (pad.rx, pad.ry, t.stickDeadzone);
    const auto sq = pad::discToSquare (dz.x, dz.y);

    auto body = area.reduced (12).withTrimmedTop (18);
    auto plot = body.removeFromLeft (body.getHeight());
    body.removeFromLeft (14);

    stickPlot (g, plot, pad.rx, pad.ry, sq.x, sq.y, t.strumDeadzone, -1, false);

    const auto rate = pad::strumRate (sq.y, t);

    valueRows (g, body,
    {
        { "ra y",       num (pad.ry) },
        { "kvadrat y",  num (sq.y) },
        { "kvadrat x",  num (sq.x) },
        { "riktning",   rate <= 0.0f ? String ("stilla")
                          : (sq.y > 0.0f ? String ("upp") : String ("ner")) },
        { "hastighet",  String (rate, 1) + " ton/s" },
        { "intervall",  rate <= 0.0f ? String ("-")
                          : String (1000.0f / rate, 1) + " ms" },
        { "anslag",     String (pad::strumVelocity (sq.y, t) * 100.0f, 0) + " %" },
        { "zonval",     std::abs (sq.x) <= t.zoneDeadzone ? String ("mitten")
                          : (sq.x < 0.0f ? String ("vanster") : String ("hoger")) }
    });
}

void InputMonitor::drawTriggers (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "TRIGGRAR");

    const auto& t = processor.tuning();
    auto body = area.reduced (12).withTrimmedTop (20);

    const auto lt = pad::applyDeadzone1D (pad.lt, t.triggerDeadzone);
    const auto rt = pad::applyDeadzone1D (pad.rt, t.triggerDeadzone);

    meterBar (g, body.removeFromTop (26), lt, "LT",
              String (pad::ringSeconds (lt, t), 2) + " s ring", accent);

    body.removeFromTop (6);

    meterBar (g, body.removeFromTop (26), rt, "RT",
              rt <= 0.0f ? String ("tyst")
                         : String (pad::triggerDecibels (rt, t), 1) + " dB", accent);

    body.removeFromTop (10);

    g.setColour (muted);
    g.setFont (mono (10.5f));
    g.drawFittedText ("LT slapp = langsta ring, RT slapp = tyst. LT kortar av "
                      "aven det som redan klingar.",
                      body, Justification::topLeft, 3);
}

void InputMonitor::drawInstrument (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "INSTRUMENT");

    const auto& p = processor.published();

    const int  index  = p.stringIndex.load (std::memory_order_relaxed);
    const int  count  = p.stringCount.load (std::memory_order_relaxed);
    const int  note   = p.stringMidi.load (std::memory_order_relaxed);
    const int  voices = p.activeVoices.load (std::memory_order_relaxed);
    const int  zoneLo = p.zoneLow.load (std::memory_order_relaxed);
    const int  zoneHi = p.zoneHigh.load (std::memory_order_relaxed);
    const auto ringS  = p.ringSeconds.load (std::memory_order_relaxed);
    const auto db     = p.masterDb.load (std::memory_order_relaxed);

    auto body = area.reduced (12).withTrimmedTop (20);

    stringRow (g, body.removeFromTop (30), count, index, zoneLo, zoneHi);
    body.removeFromTop (6);

    const bool whole = (zoneLo <= 0 && zoneHi >= count - 1);

    const int voice  = p.voiceIndex.load (std::memory_order_relaxed);
    const int octave = p.octave.load (std::memory_order_relaxed);

    valueRows (g, body,
    {
        { "rost",   String (dsp::voiceSpec (voice).name)
                      + "   oktav " + (octave > 0 ? "+" : "") + String (octave) },
        { "zon",    whole ? String ("hela plattan")
                          : String (zoneLo / 3 + 1) + " av 4" },
        { "plats",  index < 0 ? String ("-")
                      : String (index) + " av " + String (count) },
        { "ton",    noteName (note) + "   ring " + String (ringS, 1) + " s" },
        { "master", db <= processor.tuning().triggerFloorDb + 0.1f
                      ? String ("tyst, reverbet lever") : String (db, 1) + " dB" }
    }, 11.5f);

    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("D-pad vanster/hoger byter rost, " + String (voices) + " roster ljuder",
                area.reduced (12).removeFromBottom (14), Justification::centredLeft);
}

void InputMonitor::drawButtons (Graphics& g, Rectangle<int> area)
{
    panelBox (g, area, "KNAPPAR - TILLSTAND OCH RAKNADE TRYCK");

    const auto& p = processor.published();

    auto body = area.reduced (12).withTrimmedTop (20);
    auto footer = body.removeFromBottom (32);   // two lines: comp state, then the map

    const int cols = 7;
    const int cellW = body.getWidth() / cols;
    const int cellH = jmin (46, body.getHeight() / 2);

    for (int i = 0; i < kNumButtons; ++i)
    {
        auto cell = Rectangle<int> (body.getX() + (i % cols) * cellW,
                                    body.getY() + (i / cols) * cellH,
                                    cellW - 6, cellH - 6);

        const bool down = (pad.buttons & kButtons[i].bit) != 0;
        const int  slot = slotFor (kButtons[i].bit);

        g.setColour (down ? accent : hair);
        g.fillRect (cell);

        g.setColour (down ? ground : ink);
        g.setFont (mono (12.0f, true));
        g.drawText (kButtons[i].name, cell.reduced (6).removeFromTop (16),
                    Justification::centredLeft);

        g.setColour (down ? ground.withAlpha (0.8f) : muted);
        g.setFont (mono (10.5f));
        g.drawText (slot < 0 ? String ("-")
                             : String (p.pressCounts[slot].load (std::memory_order_relaxed)),
                    cell.reduced (6).removeFromBottom (14),
                    Justification::centredLeft);
    }

    const int lastSlot = p.lastButton.load (std::memory_order_relaxed);
    String last = "-";

    for (int i = 0; i < kNumButtons; ++i)
    {
        if (slotFor (kButtons[i].bit) != lastSlot)
            continue;

        last = String (kButtons[i].name)
                 + (p.lastDown.load (std::memory_order_relaxed) ? " ned" : " upp");
        break;
    }

    // The accompaniment's own state, because "it sticks" is not something the
    // chord readout can answer: a held drone and a stalled clock look identical
    // from outside.
    const auto figure = (dsp::CompPattern) p.compPattern.load (std::memory_order_relaxed);

    g.setColour (p.compRunning.load (std::memory_order_relaxed) ? accent : muted);
    g.setFont (mono (10.5f, true));
    g.drawText ("KOMP " + String (p.compRunning.load (std::memory_order_relaxed) ? "GAR" : "STOPP")
                  + "  " + String (dsp::compPatternName (figure))
                  + "  steg " + String (p.compStep.load (std::memory_order_relaxed))
                  + "  roster " + String (p.compVoices.load (std::memory_order_relaxed))
                  + "  tempo " + String (p.tempo.load (std::memory_order_relaxed), 1),
                footer.removeFromTop (14), Justification::centredLeft);

    g.setColour (muted);
    g.setFont (mono (10.0f));
    g.drawText ("D-UPP/NER tonart, D-VAN/HOG rost, Y dur/moll, LB septim, "
                "VIEW/MENY oktav, LS lasar, RS tystar.  " + last,
                footer, Justification::centredLeft);
}

} // namespace chordtroller
