#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "input/MotionFusion.h"

#include <cmath>

using namespace juce;
using namespace chordtroller;

namespace
{
    /** Which counter slot a button bit belongs to. The bits are single flags, so
        this is just where the one bit sits. */
    int slotFor (std::uint16_t button) noexcept
    {
        for (int i = 0; i < ChordtrollerAudioProcessor::Published::kButtonSlots; ++i)
            if (button == (std::uint16_t) (1u << i))
                return i;

        return -1;
    }
}

ChordtrollerAudioProcessor::ChordtrollerAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Output", AudioChannelSet::stereo(), true))
{
    // Started from the constructor rather than from prepareToPlay: the editor
    // reads the pad too, and in a plugin it can be open with the transport
    // stopped. It also means hot-plugging works before a note is ever asked for.
    reader.startReading();

    // Host parameters. The ids are what a saved song finds its automation lanes
    // by, so they never change. Names are UTF-8 spelt out, so the compiler's
    // idea of the source file's encoding cannot mangle them.
    auto addFloat = [this] (const char* id, const char* utf8Name, float defaultValue)
    {
        auto* p = new AudioParameterFloat (ParameterID { id, 1 }, String::fromUTF8 (utf8Name),
                                           NormalisableRange<float> (0.0f, 1.0f), defaultValue);
        addParameter (p);
        return p;
    };

    // The levels are fader positions on a decibel law (pad::faderGain). The
    // defaults sit where the old plain-gain defaults were: -10.5 and -2 dB.
    compLevelParam     = addFloat ("komp_niva",        "Komp niv\xc3\xa5",        0.78f);
    junoLevelParam     = addFloat ("juno_niva",        "Juno niv\xc3\xa5",        0.96f);
    junoCutoffParam    = addFloat ("juno_cutoff",      "Juno cutoff",             0.0f);
    junoResonanceParam = addFloat ("juno_resonans",    "Juno resonans",           0.0f);
    thereminLockParam  = addFloat ("theremin_lasning", "Theremin l\xc3\xa5sning", 0.0f);
    acidLevelParam     = addFloat ("acid_niva",        "303 niv\xc3\xa5",         0.96f);
    acidCutoffParam    = addFloat ("acid_cutoff",      "303 cutoff",              0.0f);
    acidResonanceParam = addFloat ("acid_resonans",    "303 resonans",            0.0f);

    acidEnv      = std::make_unique<PadParameter> (*addFloat ("acid_env",      "303 env mod",        0.5f));
    acidDecay    = std::make_unique<PadParameter> (*addFloat ("acid_decay",    "303 decay",          0.4f));
    acidMix      = std::make_unique<PadParameter> (*addFloat ("acid_mix",      "303 delay mix",      0.0f));
    acidFeedback = std::make_unique<PadParameter> (*addFloat ("acid_feedback", "303 delay feedback", 0.65f));

    // Last, so the ones above keep their places in any host that goes by order.
    stringLevelParam    = addFloat ("strak_niva",  "Str\xc3\xa5kar niv\xc3\xa5",  0.96f);
    stringPressureParam = addFloat ("strak_tryck", "Str\xc3\xa5kar tryck",      0.0f);
    arpLevelParam       = addFloat ("arp_niva",    "Arp niv\xc3\xa5",            0.96f);
    arpCutoffParam      = addFloat ("arp_cutoff",  "Arp cutoff",                0.0f);

    startTimerHz (30);

    // The tonic, straight up, is where the ring is at rest. Waiting for the
    // stick to be pushed once before anything can sound would be a puzzle, not
    // a feature.
    currentChord = ring.chordAt (chordSector);

    // The screen and a save both read the published pattern, and its atomics
    // start at zero — which unpacks as sixteen notes on the root, not the empty
    // pattern the engine really holds. Published once here, so a project saved
    // before the 303 is ever opened saves what is actually in it.
    publishAcidPattern();
}

void ChordtrollerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engine.setChord (currentChord);

    ensemble.prepare (sampleRate);
    ensemble.setRate (0.5f);        // Juno Chorus I territory, where the Omnichord lives
    ensemble.setMix (0.45f);

    reverb.setSampleRate (sampleRate);

    juce::Reverb::Parameters room;
    room.roomSize   = 0.62f;
    room.damping    = 0.42f;
    room.wetLevel   = 0.30f;
    room.dryLevel   = 1.00f;
    room.width      = 1.00f;
    room.freezeMode = 0.0f;
    reverb.setParameters (room);
    reverb.reset();

    midiOut.prepare (sampleRate);

    comp.prepare (sampleRate);
    comp.setChord (currentChord);

    theremin.prepare (sampleRate);

    junoEngine.prepare (sampleRate);
    junoDelay.prepare (sampleRate);

    acidEngine.prepare (sampleRate);
    acidEngine.setScale (dsp::acidScaleAt (acidScaleSlot));
    acidEngine.setGlide (acidGlideIndex);
    acidEngine.setOctave (acidOctaveShift);
    acidEngine.setSquare (acidSquare);
    acidEngine.seed ((std::uint32_t) Random::getSystemRandom().nextInt());
    acidDelay.prepare (sampleRate);

    arp.prepare (sampleRate);
    arp.seed ((std::uint32_t) Random::getSystemRandom().nextInt());
    arpDelay.prepare (sampleRate);

    strings.prepare (sampleRate);
    strings.seed ((std::uint32_t) Random::getSystemRandom().nextInt());
    stringPizzMidi.prepare (sampleRate);

    // Five milliseconds to open, a hundred and fifty to close.
    masterLevel = 0.0f;
    openCoef  = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.005));
    closeCoef = 1.0f - std::exp (-1.0f / (float) (sampleRate * 0.150));
}

bool ChordtrollerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

void ChordtrollerAudioProcessor::clearCounters() noexcept
{
    for (auto& c : pub.pressCounts)
        c.store (0, std::memory_order_relaxed);

    pub.lastButton.store (-1, std::memory_order_relaxed);
}

void ChordtrollerAudioProcessor::cycleScaleFamily (int family) noexcept
{
    theremin.setScale (music::stepScaleFamily (theremin.scale(), family));
}

void ChordtrollerAudioProcessor::handleButton (const PadEvent& e) noexcept
{
    const int slot = slotFor (e.button);

    if (slot >= 0 && e.down)
        pub.pressCounts[slot].fetch_add (1, std::memory_order_relaxed);

    pub.lastButton.store (slot, std::memory_order_relaxed);
    pub.lastDown.store (e.down, std::memory_order_relaxed);
    pub.lastTimeMs.store (e.timeMs, std::memory_order_relaxed);

    if (blockInstrument == Instrument::acid)
    {
        handleAcidButton (e);
        return;
    }

    if (blockInstrument == Instrument::strings)
    {
        handleStringButton (e);
        return;
    }

    if (blockInstrument == Instrument::arp)
    {
        handleArpButton (e);
        return;
    }

    if (blockInstrument == Instrument::juno)
    {
        handleJunoButton (e);
        return;
    }

    if (! e.down)
    {
        if (blockInstrument == Instrument::theremin && e.button == padLB)
            theremin.setFrozen (false);

        return;
    }

    if (blockInstrument == Instrument::theremin)
    {
        switch (e.button)
        {
            // The shared spine: these mean the same thing in both instruments,
            // so switching costs you the sticks and triggers and nothing else.
            case padDpadUp:    ring.transposeByFifths (1);  break;
            case padDpadDown:  ring.transposeByFifths (-1); break;
            case padDpadLeft:  theremin.setTimbre (theremin.timbre() - 1); break;
            case padDpadRight: theremin.setTimbre (theremin.timbre() + 1); break;

            case padView:      thereminOctave = thereminOctave >= 2 ? -2 : thereminOctave + 1;
                               theremin.setOctaveShift (thereminOctave);
                               break;

            case padMenu:      theremin.cycleGlide(); break;

            // Held, not toggled: freezing is something you do for a moment while
            // the other hand sets a vibrato, not a mode you leave on.
            case padLB:        theremin.setFrozen (true); break;

            // Motion on and off. A controller without sensors ignores it, and
            // the panel says why.
            case padRB:        toggleThereminMotion(); break;

            case padA:         cycleScaleFamily (0); break;
            case padB:         cycleScaleFamily (1); break;
            case padX:         cycleScaleFamily (2); break;
            case padY:         theremin.cycleRange(); break;

            case padLS:        thereminLatched = ! thereminLatched; break;
            case padRS:        theremin.allOff(); thereminLatched = false; break;

            default: break;
        }

        return;
    }

    switch (e.button)
    {
        case padDpadUp:    ring.transposeByFifths (1);           break;
        case padDpadDown:  ring.transposeByFifths (-1);          break;

        // The plan left these two doing nothing until v2 brought a tempo for
        // them to change. Voices need somewhere to live and this is a dead
        // control, so they take it.
        case padDpadLeft:  engine.setVoice (engine.voice() - 1); break;
        case padDpadRight: engine.setVoice (engine.voice() + 1); break;

        // VIEW cycles the octave rather than only lowering it, which freed MENY
        // for the drums. The pad had nothing left: fourteen buttons, two stick
        // clicks and two triggers, all spoken for. Octave shift is a thing you
        // set between songs; drums are a thing you throw in mid-song, so the
        // drums took the button you can reach without letting go of a stick.
        case padView:      engine.setOctaveShift (engine.octave() >= 2 ? -2
                                                                       : engine.octave() + 1);
                           break;

        case padMenu:      comp.toggleDrums(); break;

        // A face button, which the right thumb can only reach by letting go of
        // the strum. That makes it wrong for anything changed mid-phrase and
        // right for a key signature — see the note in the readme about what this
        // implies for the accompaniment buttons planned for the same four.
        case padY:         ring.toggleMode(); break;

        // RB is a shoulder, so the index finger reaches it without either thumb
        // letting go — which is what start and stop needs to be. The patterns
        // sit on face buttons because choosing one is a between-songs decision.
        case padRB:        comp.toggle(); break;

        // Each face button owns a family of three and steps through it, so nine
        // figures fit on three buttons and each button still means one thing:
        // A holds, B marches, X swings.
        case padA:         comp.cycleFamily (0); break;
        case padB:         comp.cycleFamily (1); break;
        case padX:         comp.cycleFamily (2); break;

        // Latching frees the left thumb entirely, which is the Omnichord's
        // manual mode: hold a drone and pick over it with both hands free.
        case padLS:        chordLatched = ! chordLatched; break;

        case padRS:
            engine.allNotesOff();
            flushMidi = true;
            break;

        default: break;
    }
}

void ChordtrollerAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;

    buffer.clear();
    midi.clear();      // we are a source, not a filter — nothing arrives here

    // Whatever this block ends up putting in the MIDI buffer also goes to the
    // chosen system port, since Reason drops a plugin's own MIDI out. A guard
    // rather than a call at the end, so every early return below is covered.
    struct SendOnExit
    {
        MidiPortSender& port;
        MidiBuffer&     block;
        double          rate;

        ~SendOnExit() { port.push (block, rate); }
    } sendOnExit { midiPort, midi, getSampleRate() };

    // One reading of the instrument for the whole block. The editor can switch
    // it at any moment, and a block that handled its buttons as one instrument
    // and rendered as another would send notes nobody ever releases.
    blockInstrument = instrument();

    // The Juno's note list for this block is opened before the buttons are
    // handled, because its buttons strike and release notes too — RS lets go of
    // everything, X and VIEW move the chord, MENY can strike or drop a pad.
    // Opened any later, those events were wiped: the pad fell silent and its
    // releases never reached the host.
    if (blockInstrument == Instrument::juno)
        junoEngine.beginBlock();
    else if (blockInstrument == Instrument::acid)
        acidEngine.beginBlock();    // the same reason: MENY, A and RS strike and release too
    else if (blockInstrument == Instrument::arp)
        arp.beginBlock();           // and again: the d-pad's sound change releases notes

    // Before the buttons, so a restored pattern is what the first press of this
    // block acts on.
    applyAcidRestore();

    // The queue has exactly one consumer and it is this one. Anything else
    // reading it would take events away from the instrument, so the screen gets
    // counters rather than the events themselves.
    PadEvent event;

    while (reader.popEvent (event))
        handleButton (event);

    if (flushMidi)
    {
        midiOut.allOff (midi, 0);
        flushMidi = false;
    }

    const auto state = reader.state();
    const auto& t = padTuning;

    // Leaving the Juno mid-phrase would strand its notes in the host: nothing
    // else is ever going to send their note-offs.
    const int nowInstrument = (int) blockInstrument;

    if (previousInstrument == (int) Instrument::juno && nowInstrument != (int) Instrument::juno)
    {
        junoEngine.beginBlock();
        junoEngine.allOff();

        for (int i = 0; i < junoEngine.eventsThisBlock(); ++i)
            if (! junoEngine.event (i).on)
                midi.addEvent (MidiMessage::noteOff (1, junoEngine.event (i).note), 0);
    }

    if (previousInstrument == (int) Instrument::acid && nowInstrument != (int) Instrument::acid)
    {
        acidEngine.beginBlock();
        acidEngine.allOff();

        for (int i = 0; i < acidEngine.eventsThisBlock(); ++i)
            if (! acidEngine.event (i).on)
                midi.addEvent (MidiMessage::noteOff (1, acidEngine.event (i).note), 0);

        // Stopping ends a take, and ending a take can hand steps back. Nothing
        // renders the 303 after this block to publish that.
        publishAcidPattern();
    }

    if (previousInstrument == (int) Instrument::arp && nowInstrument != (int) Instrument::arp)
    {
        arp.beginBlock();
        arp.allOff();

        for (int i = 0; i < arp.eventsThisBlock(); ++i)
            if (! arp.event (i).on)
                midi.addEvent (MidiMessage::noteOff (1, arp.event (i).note), 0);
    }

    // A bow still moving, or a pluck still counting down, would leave its note
    // on in the host.
    if (previousInstrument == (int) Instrument::strings && nowInstrument != (int) Instrument::strings)
    {
        stringNotesOff (midi, 0);
        strings.allOff();
    }

    previousInstrument = nowInstrument;

    if (blockInstrument == Instrument::theremin)
    {
        renderTheremin (buffer, state);
        return;
    }

    if (blockInstrument == Instrument::acid)
    {
        renderAcid (buffer, midi, state);
        return;
    }

    if (blockInstrument == Instrument::strings)
    {
        renderStrings (buffer, midi, state);
        return;
    }

    if (blockInstrument == Instrument::arp)
    {
        renderArp (buffer, midi, state);
        return;
    }

    if (blockInstrument == Instrument::juno)
    {
        renderJuno (buffer, midi, state);
        return;
    }

    // The chorus is shared with the Juno, which sets its own rate and depth, so
    // the strings put theirs back every block rather than inheriting a Juno's.
    ensemble.setRate (0.5f);
    ensemble.setMix (0.45f);

    // LB is momentary: held, it colours whatever the left thumb is already
    // pointing at, and letting go puts it back.
    ring.setBank ((state.buttons & padLB) != 0 ? 1 : 0);

    // Left stick: chord. Latched, the stick is ignored entirely.
    const auto leftDz = pad::applyDeadzone (state.lx, state.ly, t.stickDeadzone);
    const auto left   = pad::discToSquare (leftDz.x, leftDz.y);

    if (! chordLatched)
        chordSector = pad::sectorWithHysteresis (left.x, left.y, chordSector, t);

    const auto chord = ring.chordAt (chordSector);

    if (chord != currentChord)
    {
        currentChord = chord;
        engine.setChord (chord);
        comp.setChord (chord);
    }

    // The host's tempo when there is one, ours when there is not — which is
    // every standalone run and any DAW sitting stopped. A playing host also
    // puts the accompaniment's steps on its bars.
    const auto host      = readHostTransport();
    const bool hostTempo = host.bpm > 0.0;

    comp.setTempo (hostTempo ? host.bpm : ownTempo.load (std::memory_order_relaxed));
    comp.followHost (host.playing, host.ppq);
    comp.setLevel (pad::faderGain (compLevelParam->get()));

    pub.hostPlaying.store (host.playing, std::memory_order_relaxed);

    // Right stick: how fast, which way, and how hard the strings are held.
    const auto rightDz = pad::applyDeadzone (state.rx, state.ry, t.stickDeadzone);
    const auto right   = pad::discToSquare (rightDz.x, rightDz.y);

    engine.setStrumAxis (right.y, t);
    engine.setZone (right.x, t);

    const auto leftTrigger  = pad::applyDeadzone1D (state.lt, t.triggerDeadzone);
    const auto rightTrigger = pad::applyDeadzone1D (state.rt, t.triggerDeadzone);

    engine.setRing (leftTrigger, t);

    // Right trigger: volume. At rest it sits on the floor, which is silence —
    // the trigger is the whole level, the way a swell pedal is.
    const auto db = pad::triggerDecibels (rightTrigger, t);
    const auto targetGain = Decibels::decibelsToGain (db, t.triggerFloorDb);

    const int numSamples  = buffer.getNumSamples();
    const bool stereo     = buffer.getNumChannels() > 1;

    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = stereo ? buffer.getWritePointer (1) : nullptr;

    engine.beginBlock();

    for (int i = 0; i < numSamples; ++i)
    {
        // Asymmetric: opens in five milliseconds, closes in a hundred and fifty.
        masterLevel += (targetGain - masterLevel)
                         * (targetGain > masterLevel ? openCoef : closeCoef);

        const float dry = engine.processSample() * masterLevel;

        float wetLeft = 0.0f, wetRight = 0.0f;
        ensemble.process (dry, wetLeft, wetRight);

        // Deliberately outside both the swell and the chorus. The trigger is a
        // pedal for the strings, and an accompaniment that dies whenever the
        // strumming hand rests is not an accompaniment. The chorus is deep
        // enough to wash a bass line into mush, so the comp keeps its own dry
        // centre and meets the strings at the reverb.
        const float accompaniment = comp.processSample();

        if (stereo)
        {
            leftOut[i]  = wetLeft  + accompaniment;
            rightOut[i] = wetRight + accompaniment;
        }
        else
        {
            leftOut[i] = (wetLeft + wetRight) * 0.5f + accompaniment;
        }
    }

    // After the volume on purpose. Letting go of the trigger stops feeding the
    // room but does not empty it, so the instrument stops rather than being
    // switched off — which is the whole difference between a swell pedal and a
    // mute button.
    if (stereo)
        reverb.processStereo (leftOut, rightOut, numSamples);
    else
        reverb.processMono (leftOut, numSamples);

    // MIDI last, from what the strum clock recorded on its way through the
    // block. Retiring first means a note struck again this block releases the
    // old one at the right sample rather than after it.
    const int plucks = engine.plucks();

    midiOut.expire (midi, numSamples);

    for (int i = 0; i < plucks; ++i)
    {
        const auto& e = engine.pluck (i);
        midiOut.noteOn (midi, e.midiNote, e.velocity, e.sampleOffset,
                        engine.ringSeconds(), numSamples);
    }

    if (plucks > 0 && rumbleOn.load (std::memory_order_relaxed))
        reader.requestRumble (0.5f * engine.lastPluckStrength());

    pub.sector.store (chordSector, std::memory_order_relaxed);
    pub.keyRoot.store (ring.keyRoot(), std::memory_order_relaxed);
    pub.stringIndex.store (engine.stringIndex(), std::memory_order_relaxed);
    pub.stringCount.store (engine.stringCount(), std::memory_order_relaxed);
    pub.stringMidi.store (engine.currentMidi(), std::memory_order_relaxed);
    pub.zoneLow.store (engine.zoneLow(), std::memory_order_relaxed);
    pub.zoneHigh.store (engine.zoneHigh(), std::memory_order_relaxed);
    pub.voiceIndex.store (engine.voice(), std::memory_order_relaxed);
    pub.activeVoices.store (engine.activeVoices(), std::memory_order_relaxed);
    pub.ringSeconds.store (engine.ringSeconds(), std::memory_order_relaxed);
    pub.masterDb.store (db, std::memory_order_relaxed);
    pub.octave.store (engine.octave(), std::memory_order_relaxed);
    pub.bank.store (ring.currentBank(), std::memory_order_relaxed);
    pub.minorMode.store (ring.isMinor(), std::memory_order_relaxed);
    pub.compRunning.store (comp.isRunning(), std::memory_order_relaxed);
    pub.compPattern.store ((int) comp.currentPattern(), std::memory_order_relaxed);
    pub.tempo.store ((float) comp.tempo(), std::memory_order_relaxed);
    pub.tempoFromHost.store (hostTempo, std::memory_order_relaxed);
    pub.compStep.store (comp.currentStep(), std::memory_order_relaxed);
    pub.compVoices.store (comp.activeVoices(), std::memory_order_relaxed);
    pub.drumsOn.store (comp.drumsEnabled(), std::memory_order_relaxed);
    pub.chordLatched.store (chordLatched, std::memory_order_relaxed);
}

void ChordtrollerAudioProcessor::toggleThereminMotion() noexcept
{
    if (thereminMotion)
    {
        thereminMotion = false;
        return;
    }

    // Home is wherever the hands are right now.
    const auto now = reader.state();

    if (! now.rightHand.valid)
        return;

    thereminHomeRight = now.rightHand;
    thereminHomeLeft  = now.leftHand;
    thereminMotion    = true;
}

void ChordtrollerAudioProcessor::renderTheremin (AudioBuffer<float>& buffer, const PadState& state)
{
    const auto& t = padTuning;

    const auto rightDz = pad::applyDeadzone (state.rx, state.ry, t.stickDeadzone);
    const auto right   = pad::discToSquare (rightDz.x, rightDz.y);

    const auto leftDz  = pad::applyDeadzone (state.lx, state.ly, t.stickDeadzone);
    const auto left    = pad::discToSquare (leftDz.x, leftDz.y);

    const auto leftTrigger  = pad::applyDeadzone1D (state.lt, t.triggerDeadzone);
    const auto rightTrigger = pad::applyDeadzone1D (state.rt, t.triggerDeadzone);

    theremin.setKeyRoot (ring.keyRoot());

    // Motion mode: the right hand is the pitch antenna. Pointing up from home
    // raises the pitch and twisting the wrist bends it, both measured from where
    // the hand was when RB went down. Nothing smooths the hand's own tremor away,
    // so a trembling wrist is the vibrato a thereminist actually plays.
    const bool motion = thereminMotion && state.rightHand.valid;

    constexpr float kPitchSpan  = 50.0f * MathConstants<float>::pi / 180.0f;   // either side of home: the whole range
    constexpr float kTwistSpan  = 30.0f * MathConstants<float>::pi / 180.0f;   // either side: a full bend
    constexpr float kVolumeSpan = 45.0f * MathConstants<float>::pi / 180.0f;   // home is silence, this is full

    if (motion)
        theremin.setPitchAxis (jlimit (-1.0f, 1.0f, motionPitch (thereminHomeRight, state.rightHand) / kPitchSpan),
                               jlimit (-1.0f, 1.0f, motionTwist (thereminHomeRight, state.rightHand) / kTwistSpan));
    else
        theremin.setPitchAxis (right.y, right.x);
    theremin.setModulation (left.y, left.x);
    // The locking parameter is a floor under RT, like the filter floors.
    const float lockFloor = thereminLockParam->get();
    theremin.setQuantise (lockFloor + (1.0f - lockFloor) * rightTrigger);

    // The left trigger is the whole articulation, so it gets the same curve the
    // strings' swell got — it bites early rather than waking up at the end of
    // its throw. Latched, the note simply stays up so both hands are free.
    // With a Joy-Con pair the left hand is the volume antenna: raised from home
    // it swells, through the same taper as the trigger so it wakes early. A
    // single controller has one set of sensors and they are already the pitch,
    // so it keeps the trigger — which on a Joy-Con is only on or off.
    const bool  leftHandPlays = motion && state.leftHand.valid && thereminHomeLeft.valid;
    const float swell = leftHandPlays
                          ? jlimit (0.0f, 1.0f, motionPitch (thereminHomeLeft, state.leftHand) / kVolumeSpan)
                          : leftTrigger;

    const auto db = pad::triggerDecibels (swell, t);

    theremin.setVolume (thereminLatched
                          ? 1.0f
                          : Decibels::decibelsToGain (db, t.triggerFloorDb));

    theremin.updateTarget();

    const int  numSamples = buffer.getNumSamples();
    const bool stereo     = buffer.getNumChannels() > 1;

    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = stereo ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // No chorus: a theremin has none, and the ensemble would smear the one
        // thing the instrument is made of. Straight into the room instead.
        const float sample = theremin.processSample();

        leftOut[i] = sample;

        if (rightOut != nullptr)
            rightOut[i] = sample;
    }

    if (stereo)
        reverb.processStereo (leftOut, rightOut, numSamples);
    else
        reverb.processMono (leftOut, numSamples);

    pub.keyRoot.store (ring.keyRoot(), std::memory_order_relaxed);
    pub.rawMidi.store (theremin.rawMidi(), std::memory_order_relaxed);
    pub.soundingMidi.store (theremin.soundingMidi(), std::memory_order_relaxed);
    pub.centsOff.store (theremin.centsOff(), std::memory_order_relaxed);
    pub.quantise.store (theremin.quantiseAmount(), std::memory_order_relaxed);
    pub.scaleIndex.store (theremin.scale(), std::memory_order_relaxed);
    pub.timbreIndex.store (theremin.timbre(), std::memory_order_relaxed);
    pub.rangeOctaves.store (theremin.rangeInOctaves(), std::memory_order_relaxed);
    pub.glideIndex.store (theremin.glide(), std::memory_order_relaxed);
    pub.vibrato.store (std::max (0.0f, left.y), std::memory_order_relaxed);
    pub.tremolo.store (std::max (0.0f, -left.y), std::memory_order_relaxed);
    pub.lfoRate.store (theremin.modulationRate(), std::memory_order_relaxed);
    pub.pitchFrozen.store (theremin.isFrozen(), std::memory_order_relaxed);
    pub.chordLatched.store (thereminLatched, std::memory_order_relaxed);
    pub.masterDb.store (db, std::memory_order_relaxed);
    pub.motionAvailable.store (state.rightHand.valid, std::memory_order_relaxed);
    pub.thereminMotion.store (motion, std::memory_order_relaxed);
    pub.motionVolume.store (leftHandPlays ? swell : -1.0f, std::memory_order_relaxed);
}

//==============================================================================

void ChordtrollerAudioProcessor::handleJunoButton (const PadEvent& e) noexcept
{
    // LB is read from the snapshot each block — it is held, not pressed — so
    // releases need nothing here.
    if (! e.down)
        return;

    switch (e.button)
    {
        // The shared spine: the same jobs as in the other two instruments.
        case padDpadUp:    ring.transposeByFifths (1);  break;
        case padDpadDown:  ring.transposeByFifths (-1); break;
        case padDpadLeft:  junoEngine.setPatch (junoEngine.patch() - 1); break;
        case padDpadRight: junoEngine.setPatch (junoEngine.patch() + 1); break;

        case padView:
            junoOctaveShift = junoOctaveShift >= 2 ? -2 : junoOctaveShift + 1;
            junoEngine.setOctave (junoOctaveShift);
            break;

        case padMenu:      junoEngine.cycleVariation(); break;

        // Face buttons are for what you set between phrases: the right thumb
        // has to leave the stick to reach them, and leaving the stick stops
        // the phrase unless the right stick click is holding it.
        case padA:         junoChorusMode = (junoChorusMode + 1) % 4; break;
        case padB:         junoGenreIndex = (junoGenreIndex + 1) % music::kNumGenres; break;
        case padX:         junoEngine.setBass (! junoEngine.bassEnabled()); break;
        case padY:         ring.toggleMode(); break;

        // Both locks live on the stick clicks they lock: press the left stick
        // and the chord stays, press the right stick and the phrase stays. The
        // hold was on RB, where the finger could not feel which stick it was
        // freezing. That costs the shared spine its panic button on RS, which is
        // where the other two instruments keep theirs — but a lock you press
        // through the thing being locked is worth the split.
        case padLS:        chordLatched = ! chordLatched;  break;
        case padRS:        junoEngine.toggleHold();        break;

        // Panic takes the shoulder the hold left behind.
        case padRB:        junoEngine.allOff();            break;
        default: break;
    }
}

void ChordtrollerAudioProcessor::renderJuno (AudioBuffer<float>& buffer, MidiBuffer& midi,
                                             const PadState& state)
{
    const auto& t = padTuning;

    // The note list was opened at the top of processBlock, before the buttons —
    // see there. Nothing here may open it again.

    const auto leftDz  = pad::applyDeadzone (state.lx, state.ly, t.stickDeadzone);
    const auto left    = pad::discToSquare (leftDz.x, leftDz.y);
    const auto rightDz = pad::applyDeadzone (state.rx, state.ry, t.stickDeadzone);
    const auto right   = pad::discToSquare (rightDz.x, rightDz.y);

    const auto leftTrigger  = pad::applyDeadzone1D (state.lt, t.triggerDeadzone);
    const auto rightTrigger = pad::applyDeadzone1D (state.rt, t.triggerDeadzone);

    // Chord. The ring's seventh bank says what *kind* of chord each place is;
    // the genre decides what that kind sounds like, and LB reaches past it.
    ring.setBank (1);

    if (! chordLatched)
        chordSector = pad::sectorWithHysteresis (left.x, left.y, chordSector, t);

    const bool extended = (state.buttons & padLB) != 0;
    const auto chord    = ring.chordAt (chordSector);

    junoEngine.setChord (chord.rootPitchClass, music::functionOf (chord.quality),
                         extended, junoGenreIndex);

    // Tempo, from the host when it has one — the phrases have to sit in a song.
    const auto   host      = readHostTransport();
    const bool   hostTempo = host.bpm > 0.0;
    const double bpm       = hostTempo ? host.bpm : ownTempo.load (std::memory_order_relaxed);

    pub.hostPlaying.store (host.playing, std::memory_order_relaxed);

    junoEngine.setTempo (bpm);
    junoEngine.followHost (host.playing, host.ppq);

    // Phrase. The stick only points: which way it leans picks the style, and
    // whether it is out at all is play or stop. Nothing reads the distance any
    // more — aiming at a style and leaning on it were fighting each other in the
    // same gesture, and the distance always lost.
    const float radius = std::sqrt (right.x * right.x + right.y * right.y);
    const bool  pushed = radius >= t.chordHoldRadius;

    junoPhraseSector = pushed ? pad::sectorWithHysteresis (right.x, right.y, junoPhraseSector, t)
                              : -1;

    // How hard is a constant now. It was on the right trigger for a while and
    // before that on the stick's distance, and both were worth less than a
    // dedicated filter control — a synthesiser's two triggers belong to the
    // filter. Set every block rather than once, so it stays one line to give
    // back to a control if it is ever missed.
    constexpr float kFixedIntensity = 0.8f;

    junoEngine.setIntensity (kFixedIntensity);
    junoEngine.setPhrase (junoPhraseSector);

    // Filter: cutoff right, resonance left — see filterSweep, which the panel
    // calls too so the bars cannot drift from the voices.
    // The host parameters are floors: the trigger takes the rest of the way up
    // from wherever automation or Remote has left them.
    const float cutoffFloor    = junoCutoffParam->get();
    const float resonanceFloor = junoResonanceParam->get();
    const auto  sweep = dsp::filterSweep (cutoffFloor + (1.0f - cutoffFloor) * rightTrigger,
                                          resonanceFloor + (1.0f - resonanceFloor) * leftTrigger);
    const auto& patch = junoEngine.currentPatch();   // still owns the delay

    junoEngine.setFilter (sweep.cutoff, sweep.resonance);

    // Chorus I and II are two speeds of the same bucket brigade. I+II is an
    // approximation: the real one is faster and shallower than this chorus's
    // fixed depth allows, so it is the widest of the three rather than a copy.
    static constexpr float rates[4] { 0.50f, 0.52f, 0.83f, 1.20f };
    static constexpr float mixes[4] { 0.00f, 0.50f, 0.50f, 0.58f };

    ensemble.setRate (rates[junoChorusMode]);
    ensemble.setMix (mixes[junoChorusMode]);

    junoDelay.setTiming (bpm, patch.delayDivision);
    junoDelay.setAmount (patch.delaySend, patch.delayFeedback);

    const float level = pad::faderGain (junoLevelParam->get());

    const int  numSamples = buffer.getNumSamples();
    const bool stereo     = buffer.getNumChannels() > 1;

    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = stereo ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Voices, then the chorus that is half of what a Juno is, then the
        // delay, then the room — the J-6's signal flow.
        const float dry = junoEngine.processSample() * level;

        float l = 0.0f, r = 0.0f;
        ensemble.process (dry, l, r);
        junoDelay.process (l, r);

        if (stereo)
        {
            leftOut[i]  = l;
            rightOut[i] = r;
        }
        else
        {
            leftOut[i] = (l + r) * 0.5f;
        }
    }

    if (stereo)
        reverb.processStereo (leftOut, rightOut, numSamples);
    else
        reverb.processMono (leftOut, numSamples);

    // A chord generator's first job in a DAW is to hand the chords over, so
    // every note goes out as MIDI at the sample it sounded.
    for (int i = 0; i < junoEngine.eventsThisBlock(); ++i)
    {
        const auto& e = junoEngine.event (i);
        const int offset = jlimit (0, jmax (0, numSamples - 1), e.offset);

        midi.addEvent (e.on ? MidiMessage::noteOn (1, e.note,
                                  (uint8) jlimit (1, 127, (int) std::lround (e.velocity * 127.0f)))
                            : MidiMessage::noteOff (1, e.note),
                       offset);
    }

    pub.sector.store (chordSector, std::memory_order_relaxed);
    pub.keyRoot.store (ring.keyRoot(), std::memory_order_relaxed);
    pub.minorMode.store (ring.isMinor(), std::memory_order_relaxed);
    pub.chordLatched.store (chordLatched, std::memory_order_relaxed);
    pub.tempo.store ((float) bpm, std::memory_order_relaxed);
    pub.tempoFromHost.store (hostTempo, std::memory_order_relaxed);

    pub.junoPatch.store (junoEngine.patch(), std::memory_order_relaxed);
    pub.junoGenre.store (junoGenreIndex, std::memory_order_relaxed);
    pub.junoChorus.store (junoChorusMode, std::memory_order_relaxed);
    pub.junoBass.store (junoEngine.bassEnabled(), std::memory_order_relaxed);
    pub.junoExtended.store (extended, std::memory_order_relaxed);
    pub.junoStyle.store (junoEngine.currentStyleIndex(), std::memory_order_relaxed);
    pub.junoLastStyle.store (junoEngine.lastStyleIndex(), std::memory_order_relaxed);
    pub.junoVariation.store (junoEngine.variationOf (junoEngine.lastStyleIndex()),
                             std::memory_order_relaxed);
    pub.junoHold.store (junoEngine.isHolding(), std::memory_order_relaxed);
    pub.junoCutoff.store (sweep.cutoff, std::memory_order_relaxed);
    pub.junoResonance.store (sweep.resonance, std::memory_order_relaxed);
    pub.junoOctave.store (junoOctaveShift, std::memory_order_relaxed);

    std::array<int, dsp::JunoEngine::kVoices> sounding {};
    const int count = junoEngine.soundingNotes (sounding);

    for (int i = 0; i < dsp::JunoEngine::kVoices; ++i)
        pub.junoSounding[i].store (i < count ? sounding[(size_t) i] : -1, std::memory_order_relaxed);

    pub.junoSoundingCount.store (count, std::memory_order_relaxed);
}

//== 303 =====================================================================

void ChordtrollerAudioProcessor::handleAcidButton (const PadEvent& e) noexcept
{
    // LB is shift. Held, the d-pad and the face buttons change from writing
    // steps to changing settings — the original's FUNCTION key, and the only
    // way fourteen buttons hold both a step editor and a synth.
    if (e.button == padLB)
    {
        acidShift = e.down;
        return;
    }

    if (! e.down)
        return;

    // The same in both layers.
    if (e.button == padMenu)
    {
        if (acidEngine.isRunning())
            acidEngine.stop();
        else
            acidEngine.play();

        return;
    }

    // A stick click is easy to hit by accident. On this one that costs nothing:
    // the waveform flips back.
    if (e.button == padLS)
    {
        acidSquare = ! acidSquare;
        acidEngine.setSquare (acidSquare);
        return;
    }

    if (acidShift)
    {
        switch (e.button)
        {
            // Semitones, the way the original transposes, not the chord
            // ring's fifths.
            case padDpadUp:   ring.setKeyRoot (ring.keyRoot() + 1); break;
            case padDpadDown: ring.setKeyRoot (ring.keyRoot() - 1); break;

            case padDpadLeft:
                acidOctaveShift = jmax (-1, acidOctaveShift - 1);
                acidEngine.setOctave (acidOctaveShift);
                break;

            case padDpadRight:
                acidOctaveShift = jmin (2, acidOctaveShift + 1);
                acidEngine.setOctave (acidOctaveShift);
                break;

            case padY:
                acidScaleSlot = (acidScaleSlot + 1) % dsp::kAcidScaleSlots;
                acidEngine.setScale (dsp::acidScaleAt (acidScaleSlot));
                break;

            case padB:
                acidGlideIndex = (acidGlideIndex + 1) % dsp::kAcidGlides;
                acidEngine.setGlide (acidGlideIndex);
                break;

            case padA:
                acidEngine.setLoopEndAtCursor();
                break;

            case padX:
                acidDelayDivision = (acidDelayDivision + 1) % dsp::kAcidDelays;
                break;

            // A two-button chord is deliberate, so clearing needs no guard.
            case padRS:
                acidEngine.clearPattern();
                break;

            default:
                break;
        }

        return;
    }

    switch (e.button)
    {
        // The d-pad writes: up and down choose the note, left and right walk
        // the cursor.
        case padDpadUp:    acidEngine.changeNote (1);  break;
        case padDpadDown:  acidEngine.changeNote (-1); break;
        case padDpadLeft:  acidEngine.moveCursor (-1); break;
        case padDpadRight: acidEngine.moveCursor (1);  break;

        // Laid out the way the flicks were, so nothing has to be relearned:
        // the top button accents, the right one slides — towards the step it
        // slides into — the bottom one rests, and the left one fetches the note
        // from behind.
        case padY: acidEngine.toggleAccent(); break;
        case padB: acidEngine.toggleSlide();  break;
        case padA: acidEngine.toggleRest();   break;
        case padX: acidEngine.copyPrevious(); break;

        // A new pattern, only with the right stick at rest. That stick turns the
        // delay, and a click pressed in by accident while turning it would throw
        // a finished line away with no undo.
        case padRS:
            if (acidRightHome)
                acidEngine.randomize();
            break;

        default:
            break;
    }
}

void ChordtrollerAudioProcessor::renderAcid (AudioBuffer<float>& buffer, MidiBuffer& midi,
                                             const PadState& state)
{
    const auto& t = padTuning;

    // The note list was opened at the top of processBlock, before the buttons.

    const int    numSamples   = buffer.getNumSamples();
    const double blockSeconds = (double) numSamples / std::max (1.0, getSampleRate());

    // The events set shift as LB went down and up. The snapshot puts it right if
    // one was missed, so the screen can never be left showing the wrong layer.
    acidShift = (state.buttons & padLB) != 0;

    // Both sticks are knobs turned by leaning, not positions. A stick that
    // springs home cannot hold a value, but it can say "more" for as long as it
    // is pushed — and home then means leave it alone, which is what a stick at
    // rest ought to mean. Squared, so a small lean is a fine adjustment and a
    // full push crosses the range in about a second.
    constexpr float kKnobRate = 0.9f;
    const float step = kKnobRate * (float) blockSeconds;

    // Each knob first takes whatever the host last did to it — automation, a
    // Remote Override, a CV input — and the pad turns it from there.
    auto turn = [step] (PadParameter& knob, float lean)
    {
        knob.pull();

        if (lean != 0.0f)
            knob.turn (knob.get() + lean * std::abs (lean) * step);
    };

    const auto leftDz  = pad::applyDeadzone (state.lx, state.ly, t.stickDeadzone);
    const auto left    = pad::discToSquare (leftDz.x, leftDz.y);
    const auto rightDz = pad::applyDeadzone (state.rx, state.ry, t.stickDeadzone);
    const auto right   = pad::discToSquare (rightDz.x, rightDz.y);

    turn (*acidEnv,      left.y);     // left:  ENV MOD up and down
    turn (*acidDecay,    left.x);     //        DECAY across
    turn (*acidMix,      right.y);    // right: delay mix up and down
    turn (*acidFeedback, right.x);    //        delay feedback across

    acidRightHome = std::sqrt (right.x * right.x + right.y * right.y) < 0.2f;

    const auto   host      = readHostTransport();
    const bool   hostTempo = host.bpm > 0.0;
    const double bpm       = hostTempo ? host.bpm : ownTempo.load (std::memory_order_relaxed);

    pub.hostPlaying.store (host.playing, std::memory_order_relaxed);

    acidEngine.setTempo (bpm);
    acidEngine.followHost (host.playing, host.ppq);
    acidEngine.setKeyRoot (ring.keyRoot());

    // Cutoff right, resonance left, both from zero, with the Juno's taper on
    // the resonance.
    const auto leftTrigger  = pad::applyDeadzone1D (state.lt, t.triggerDeadzone);
    const auto rightTrigger = pad::applyDeadzone1D (state.rt, t.triggerDeadzone);
    const float cutoffFloor    = acidCutoffParam->get();
    const float resonanceFloor = acidResonanceParam->get();
    const auto  sweep = dsp::filterSweep (cutoffFloor + (1.0f - cutoffFloor) * rightTrigger,
                                          resonanceFloor + (1.0f - resonanceFloor) * leftTrigger);

    acidEngine.setFilter (sweep.cutoff, sweep.resonance);
    acidEngine.setEnvMod (acidEnv->get());
    acidEngine.setDecay (acidDecay->get());

    // Mix is a real dry/wet, not a send. The delay is always fed and the knob
    // crossfades between the dry voice and the repeats with equal power, so the
    // level holds while it turns — and being always fed, turning it up brings
    // in echoes of what has already played, the way a return fader does.
    // Feedback stops short of self-oscillation: TempoDelay clamps it at 0.85.
    // The feedback knob is how long the repeats last, in equal ratios from a
    // fifth of a second to twelve — the time an echo takes to fall 60 dB. The
    // gain per repeat follows from that and the delay time, so the same knob
    // rings as long at any tempo. Zero is no repeats at all.
    acidDelay.setTiming (bpm, acidDelayDivision);

    const float echoKnob    = acidFeedback->get();
    const double echoSeconds = 0.2 * std::pow (60.0, (double) echoKnob);
    const float echoGain    = echoKnob <= 0.0f
                                ? 0.0f
                                : (float) std::pow (10.0, -3.0 * acidDelay.delaySeconds() / echoSeconds);

    acidDelay.setAmount (1.0f, echoGain);

    const float angle   = acidMix->get() * MathConstants<float>::halfPi;
    const float dryGain = std::cos (angle);
    const float wetGain = std::sin (angle);

    const float level  = pad::faderGain (acidLevelParam->get());
    const bool  stereo = buffer.getNumChannels() > 1;

    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = stereo ? buffer.getWritePointer (1) : nullptr;

    // No chorus and no room. A 303 is dry — its space is the delay, and a bass
    // line through a hall is mud.
    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = acidEngine.processSample() * level;

        float l = dry, r = dry;
        acidDelay.process (l, r);       // adds the repeats onto the dry pair

        const float outL = dry * dryGain + (l - dry) * wetGain;
        const float outR = dry * dryGain + (r - dry) * wetGain;

        if (stereo)
        {
            leftOut[i]  = outL;
            rightOut[i] = outR;
        }
        else
        {
            leftOut[i] = (outL + outR) * 0.5f;
        }
    }

    for (int i = 0; i < acidEngine.eventsThisBlock(); ++i)
    {
        const auto& ev = acidEngine.event (i);
        const int offset = jlimit (0, jmax (0, numSamples - 1), ev.offset);

        midi.addEvent (ev.on ? MidiMessage::noteOn (1, ev.note,
                                   (uint8) jlimit (1, 127, (int) std::lround (ev.velocity * 127.0f)))
                             : MidiMessage::noteOff (1, ev.note),
                       offset);
    }

    pub.keyRoot.store (ring.keyRoot(), std::memory_order_relaxed);
    pub.tempo.store ((float) bpm, std::memory_order_relaxed);
    pub.tempoFromHost.store (hostTempo, std::memory_order_relaxed);

    publishAcidPattern();

    pub.acidPlayhead.store (acidEngine.playheadStep(), std::memory_order_relaxed);
    pub.acidCursor.store (acidEngine.cursorStep(), std::memory_order_relaxed);
    pub.acidRunning.store (acidEngine.isRunning(), std::memory_order_relaxed);
    pub.acidShift.store (acidShift, std::memory_order_relaxed);
    pub.acidGlideMode.store (acidGlideIndex, std::memory_order_relaxed);
    pub.acidDelayMode.store (acidDelayDivision, std::memory_order_relaxed);
    pub.acidDelayMix.store (acidMix->get(), std::memory_order_relaxed);
    pub.acidDelayFeedback.store (acidFeedback->get(), std::memory_order_relaxed);
    pub.acidSquare.store (acidSquare, std::memory_order_relaxed);
    pub.acidCutoff.store (sweep.cutoff, std::memory_order_relaxed);
    pub.acidResonance.store (sweep.resonance, std::memory_order_relaxed);
    pub.acidEnvMod.store (acidEnv->get(), std::memory_order_relaxed);
    pub.acidDecay.store (acidDecay->get(), std::memory_order_relaxed);
}

//== strings =================================================================

void ChordtrollerAudioProcessor::handleStringButton (const PadEvent& e) noexcept
{
    // LB and RB are octaves for as long as they are held, read from the
    // snapshot each block, so releases need nothing here.
    if (! e.down)
        return;

    switch (e.button)
    {
        // The shared spine: key on the d-pad's up and down, the voice on its
        // sides — here the instrument itself.
        case padDpadUp:    ring.transposeByFifths (1);  break;
        case padDpadDown:  ring.transposeByFifths (-1); break;
        case padDpadLeft:  strings.setInstrument (strings.instrument() - 1); break;
        case padDpadRight: strings.setInstrument (strings.instrument() + 1); break;

        case padView:      stringRegister = stringRegister >= 2 ? -2 : stringRegister + 1; break;

        // The scale families are the theremin's, on the same three buttons.
        case padA:         stringScale = music::stepScaleFamily (stringScale, 0); break;
        case padB:         stringScale = music::stepScaleFamily (stringScale, 1); break;
        case padX:         stringScale = music::stepScaleFamily (stringScale, 2); break;

        case padY:         strings.setSordino (! strings.hasSordino()); break;
        case padMenu:      strings.setSection (! strings.isSection()); break;

        // The left click locks, as it does in every instrument: here the note
        // under the finger becomes a second string, bowed with the first.
        case padLS:        stringDroneNote = stringDroneNote >= 0 ? -1 : stringLastNote; break;

        // The right click changes what the right hand does with the strings.
        // A stick already out when it goes down must come home before it can
        // pluck, or the switch itself would pluck.
        case padRS:
            strings.setPizzicato (! strings.isPizzicato());
            pizzArmed = false;
            break;

        default: break;
    }
}

void ChordtrollerAudioProcessor::stringNotesOff (MidiBuffer& midi, int sampleOffset) noexcept
{
    for (int* sent : { &stringMelodySent, &stringDroneSent })
    {
        if (*sent >= 0)
            midi.addEvent (MidiMessage::noteOff (1, *sent), sampleOffset);

        *sent = -1;
    }

    stringPizzMidi.allOff (midi, sampleOffset);
}

void ChordtrollerAudioProcessor::renderStrings (AudioBuffer<float>& buffer, MidiBuffer& midi,
                                                const PadState& state)
{
    const auto& t = padTuning;
    const int numSamples = buffer.getNumSamples();

    const auto leftDz  = pad::applyDeadzone (state.lx, state.ly, t.stickDeadzone);
    const auto left    = pad::discToSquare (leftDz.x, leftDz.y);
    const auto rightDz = pad::applyDeadzone (state.rx, state.ry, t.stickDeadzone);
    const auto right   = pad::discToSquare (rightDz.x, rightDz.y);

    const auto leftTrigger  = pad::applyDeadzone1D (state.lt, t.triggerDeadzone);
    const auto rightTrigger = pad::applyDeadzone1D (state.rt, t.triggerDeadzone);

    // The left hand. The stick points at a degree of the scale, up the tonic
    // and on round clockwise; let go and the finger stays down, so a long note
    // does not need a thumb holding it.
    if (std::sqrt (left.x * left.x + left.y * left.y) >= t.chordHoldRadius)
        stringSector = pad::sectorWithHysteresis (left.x, left.y, stringSector, t);

    // Octaves under the index fingers, held: left down, right up.
    const int octave = ((state.buttons & padRB) != 0 ? 1 : 0)
                     - ((state.buttons & padLB) != 0 ? 1 : 0);

    const int note = dsp::stringNoteFor (strings.instrument(), ring.keyRoot(), stringRegister,
                                         stringScale, stringSector, octave);
    stringLastNote = note;

    strings.setNote ((float) note);
    strings.setDrone (stringDroneNote >= 0, (float) jmax (0, stringDroneNote));

    // The right hand: up and down is the bow, sideways vibrato or tremolo. The
    // index finger leans into the bow on RT, from the host's floor; the left
    // one slows the finger's slide on LT.
    strings.setBow (right.y);
    strings.setHand (right.x);

    const float pressureFloor = stringPressureParam->get();
    const float pressure      = pressureFloor + (1.0f - pressureFloor) * rightTrigger;

    strings.setPressure (pressure);
    strings.setGlide (leftTrigger);

    // Pizzicato. Up or down both pluck; the lean has to start from home, and
    // the time it took to reach the edge is the strength — a flick is a hard
    // pluck, a push a soft one. Measured in blocks, which is as fine as the pad
    // is read anyway.
    float pluckStrength = 0.0f;

    if (strings.isPizzicato())
    {
        const float lean = std::abs (right.y);

        if (lean < 0.25f)
        {
            pizzArmed  = true;
            pizzRestAt = stringClock;
        }
        else if (pizzArmed && lean >= 0.6f)
        {
            const double took = (double) (stringClock - pizzRestAt) / jmax (1.0, getSampleRate());

            // In equal ratios of time: a flick in 12 ms is full strength, one
            // that took a quarter of a second the softest. The engine hears
            // the strength in decibels.
            pluckStrength = jlimit (0.05f, 1.0f, (float) (1.0 - std::log (jmax (took, 0.012) / 0.012)
                                                              / std::log (0.25 / 0.012)));
            strings.pluck (pluckStrength);
            pizzArmed = false;
        }
    }

    strings.beginBlock();

    const float level  = pad::faderGain (stringLevelParam->get());
    const bool  stereo = buffer.getNumChannels() > 1;

    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = stereo ? buffer.getWritePointer (1) : nullptr;

    // No chorus: a section is four players, not one smeared. Into the room,
    // which a string instrument has never been heard without.
    for (int i = 0; i < numSamples; ++i)
    {
        float l = 0.0f, r = 0.0f;
        strings.processSample (l, r);

        if (stereo)
        {
            leftOut[i]  = l * level;
            rightOut[i] = r * level;
        }
        else
        {
            leftOut[i] = (l + r) * 0.5f * level;
        }
    }

    if (stereo)
        reverb.processStereo (leftOut, rightOut, numSamples);
    else
        reverb.processMono (leftOut, numSamples);

    stringClock += numSamples;

    // Notes out. A bowed note is on for as long as a bow moves on it, and a
    // new stopped note while it moves is a legato change: the old one off, the
    // new one on, at the top of the block. How fast the bow is going is the
    // velocity. Vibrato and the glide are not sent — how far a synth bends is
    // its own business.
    const bool bowing   = strings.isBowing();
    const auto velocity = (uint8) jlimit (1, 127, (int) std::lround (40.0f + 87.0f * strings.bowSpeed()));

    const int wantMelody = bowing ? note : -1;
    const int wantDrone  = bowing && stringDroneNote >= 0 && stringDroneNote != note ? stringDroneNote : -1;

    auto follow = [&midi, velocity] (int& sent, int want)
    {
        if (sent == want)
            return;

        if (sent >= 0)
            midi.addEvent (MidiMessage::noteOff (1, sent), 0);

        if (want >= 0)
            midi.addEvent (MidiMessage::noteOn (1, want, velocity), 0);

        sent = want;
    };

    follow (stringMelodySent, wantMelody);
    follow (stringDroneSent,  wantDrone);

    // Plucks: each its own note with a countdown, the way the strummed strings
    // do it — nothing tells us when a plucked string has stopped.
    stringPizzMidi.expire (midi, numSamples);

    auto pluckOut = [this, &midi, numSamples, note] (float strength)
    {
        stringPizzMidi.noteOn (midi, note, strength, 0, 0.8f, numSamples);

        if (stringDroneNote >= 0 && stringDroneNote != note)
            stringPizzMidi.noteOn (midi, stringDroneNote, strength, 0, 0.8f, numSamples);
    };

    if (pluckStrength > 0.0f)
        pluckOut (pluckStrength);

    for (int i = 0; i < strings.autoPlucksThisBlock(); ++i)
        pluckOut (0.3f + 0.5f * strings.tremolo());

    pub.keyRoot.store (ring.keyRoot(), std::memory_order_relaxed);
    pub.stringNote.store (strings.soundingNote(), std::memory_order_relaxed);
    pub.stringOpen.store (dsp::openStringFor (strings.instrument(), strings.soundingNote()), std::memory_order_relaxed);
    pub.stringSector.store (stringSector, std::memory_order_relaxed);
    pub.stringOctave.store (octave, std::memory_order_relaxed);
    pub.stringRegister.store (stringRegister, std::memory_order_relaxed);
    pub.stringScale.store (stringScale, std::memory_order_relaxed);
    pub.stringInstrument.store (strings.instrument(), std::memory_order_relaxed);
    pub.stringBow.store (strings.bowSpeed() * (right.y >= 0.0f ? 1.0f : -1.0f), std::memory_order_relaxed);
    pub.stringVibrato.store (strings.vibrato(), std::memory_order_relaxed);
    pub.stringTremolo.store (strings.tremolo(), std::memory_order_relaxed);
    pub.stringPressure.store (pressure, std::memory_order_relaxed);
    pub.stringGlide.store (leftTrigger, std::memory_order_relaxed);
    pub.stringGlideSeconds.store (strings.glideSeconds(), std::memory_order_relaxed);
    pub.stringLevel.store (strings.outputLevel() * level, std::memory_order_relaxed);
    pub.stringBowPoint.store (strings.bowPoint(), std::memory_order_relaxed);
    pub.stringDrone.store (stringDroneNote, std::memory_order_relaxed);
    pub.stringPizz.store (strings.isPizzicato(), std::memory_order_relaxed);
    pub.stringSection.store (strings.isSection(), std::memory_order_relaxed);
    pub.stringSordino.store (strings.hasSordino(), std::memory_order_relaxed);
}

//== arpeggiator =============================================================

void ChordtrollerAudioProcessor::handleArpButton (const PadEvent& e) noexcept
{
    // VIEW is a shift while held: the d-pad and three face buttons reach the
    // settings behind it. The snapshot puts it right each block as well.
    if (e.button == padView)
    {
        arpShift = e.down;
        return;
    }

    // LB is read from the snapshot each block — it is held, not pressed.
    if (! e.down)
        return;

    if (arpShift)
    {
        switch (e.button)
        {
            case padDpadUp:    arpRegister = jmin (2, arpRegister + 1);  arp.setRegister (arpRegister); break;
            case padDpadDown:  arpRegister = jmax (-2, arpRegister - 1); arp.setRegister (arpRegister); break;
            case padDpadLeft:  arp.setOctaves (arp.octaveRange() - 1); break;
            case padDpadRight: arp.setOctaves (arp.octaveRange() + 1); break;

            case padA:         arpDelayMode = (arpDelayMode + 1) % 3; break;
            case padX:         arp.setTriplet (! arp.isTriplet()); break;
            case padY:         ring.toggleMode(); break;

            default: break;
        }

        return;
    }

    // Each face button owns two patterns and flips between them, the way the
    // accompaniment's buttons own their figures. The right thumb can reach
    // them mid-phrase now: the trigger plays, and the range stick it lets go
    // of only springs back to the whole span.
    auto pair = [this] (int first, int second)
    {
        arp.setMode (arp.modeIndex() == first ? second : first);
    };

    switch (e.button)
    {
        // The shared spine: key up and down, the voice on the sides.
        case padDpadUp:    ring.transposeByFifths (1);  break;
        case padDpadDown:  ring.transposeByFifths (-1); break;
        case padDpadLeft:  arp.setSound (arp.sound() - 1); break;
        case padDpadRight: arp.setSound (arp.sound() + 1); break;

        case padMenu:      arp.setDrums (! arp.drumsEnabled()); break;

        // Laid out as the stick's compass was: up patterns on top, down ones
        // at the bottom, there-and-back on the right, the loose ones on the
        // left.
        case padY:         pair (0, 5); break;     // UPP, OKTAVHOPP
        case padA:         pair (4, 3); break;     // NER, KONVERGERA
        case padB:         pair (2, 1); break;     // PENDEL, PEDAL
        case padX:         pair (6, 7); break;     // SLUMP, ACKORD

        // The rhythm is changed mid-phrase, so it is under the index finger.
        case padRB:        arp.setRhythm (arp.rhythmIndex() + 1); break;

        // The locks sit on the stick clicks they lock, as in the Juno.
        case padLS:        chordLatched = ! chordLatched; break;
        case padRS:        arp.toggleHold(); break;

        default: break;
    }
}

void ChordtrollerAudioProcessor::renderArp (AudioBuffer<float>& buffer, MidiBuffer& midi,
                                            const PadState& state)
{
    const auto& t = padTuning;

    // The note list was opened at the top of processBlock, before the buttons.

    const auto leftDz  = pad::applyDeadzone (state.lx, state.ly, t.stickDeadzone);
    const auto left    = pad::discToSquare (leftDz.x, leftDz.y);
    const auto rightDz = pad::applyDeadzone (state.rx, state.ry, t.stickDeadzone);
    const auto right   = pad::discToSquare (rightDz.x, rightDz.y);

    const auto leftTrigger  = pad::applyDeadzone1D (state.lt, t.triggerDeadzone);
    const auto rightTrigger = pad::applyDeadzone1D (state.rt, t.triggerDeadzone);

    // The chord: the ring, with LB reaching for the seventh bank.
    ring.setBank ((state.buttons & padLB) != 0 ? 1 : 0);

    if (! chordLatched)
        chordSector = pad::sectorWithHysteresis (left.x, left.y, chordSector, t);

    arp.setChord (ring.chordAt (chordSector));

    // The host before the stick, so a push lands on the host's grid.
    const auto   host      = readHostTransport();
    const bool   hostTempo = host.bpm > 0.0;
    const double bpm       = hostTempo ? host.bpm : ownTempo.load (std::memory_order_relaxed);

    pub.hostPlaying.store (host.playing, std::memory_order_relaxed);

    arp.setTempo (bpm);
    arp.followHost (host.playing, host.ppq);

    arpShift = (state.buttons & padView) != 0;

    // The right trigger plays: past the first few percent it runs, and its
    // travel is three wide steps of speed — an eighth, a sixteenth, a
    // thirty-second — each with a margin either side so a finger resting near
    // a line does not flicker between two.
    static constexpr float kOn = 0.06f, kOff = 0.03f;
    static constexpr float edges[2] { 0.45f, 0.85f };
    static constexpr float margin = 0.04f;

    arpPressed = arpPressed ? rightTrigger > kOff : rightTrigger > kOn;

    if (arpPressed)
    {
        while (arpRateStep < 2 && rightTrigger > edges[arpRateStep] + margin)
            ++arpRateStep;

        while (arpRateStep > 0 && rightTrigger < edges[arpRateStep - 1] - margin)
            --arpRateStep;
    }

    // The right stick is the range, as on the Omnichord: sideways narrows the
    // span towards its low or high end. Up and down is how long each note is
    // held — a blip at the bottom, tied at the top, and at rest the middle.
    arp.setZone (right.x, t.zoneDeadzone);
    arp.setGate (0.5f + 0.5f * right.y);

    // The left trigger is the filter, from the host's floor up.
    const float cutoffFloor = arpCutoffParam->get();
    const float cutoff      = cutoffFloor + (1.0f - cutoffFloor) * leftTrigger;

    arp.setCutoff (cutoff);
    arp.setRunning (arpPressed, arpRateStep);

    // The Juno's patches keep the chorus that is half their sound; the others
    // go straight to the delay and the room.
    ensemble.setRate (0.5f);
    ensemble.setMix (0.5f);

    arpDelay.setTiming (bpm, arpDelayMode == 2 ? 1 : 0);
    arpDelay.setAmount (arpDelayMode == 0 ? 0.0f : 0.35f, 0.45f);

    const float level  = pad::faderGain (arpLevelParam->get());
    const bool  chorus = arp.usesChorus();

    const int  numSamples = buffer.getNumSamples();
    const bool stereo     = buffer.getNumChannels() > 1;

    auto* leftOut  = buffer.getWritePointer (0);
    auto* rightOut = stereo ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dry = arp.processSample() * level;

        float l = dry, r = dry;

        if (chorus)
            ensemble.process (dry, l, r);

        arpDelay.process (l, r);

        if (stereo)
        {
            leftOut[i]  = l;
            rightOut[i] = r;
        }
        else
        {
            leftOut[i] = (l + r) * 0.5f;
        }
    }

    if (stereo)
        reverb.processStereo (leftOut, rightOut, numSamples);
    else
        reverb.processMono (leftOut, numSamples);

    for (int i = 0; i < arp.eventsThisBlock(); ++i)
    {
        const auto& ev = arp.event (i);
        const int offset = jlimit (0, jmax (0, numSamples - 1), ev.offset);

        midi.addEvent (ev.on ? MidiMessage::noteOn (1, ev.note,
                                   (uint8) jlimit (1, 127, (int) std::lround (ev.velocity * 127.0f)))
                             : MidiMessage::noteOff (1, ev.note),
                       offset);
    }

    pub.sector.store (chordSector, std::memory_order_relaxed);
    pub.keyRoot.store (ring.keyRoot(), std::memory_order_relaxed);
    pub.minorMode.store (ring.isMinor(), std::memory_order_relaxed);
    pub.bank.store (ring.currentBank(), std::memory_order_relaxed);
    pub.chordLatched.store (chordLatched, std::memory_order_relaxed);
    pub.tempo.store ((float) bpm, std::memory_order_relaxed);
    pub.tempoFromHost.store (hostTempo, std::memory_order_relaxed);

    pub.arpMode.store (arp.modeIndex(), std::memory_order_relaxed);
    pub.arpRate.store (arp.rateIndex(), std::memory_order_relaxed);
    pub.arpPlaying.store (arp.isPlaying(), std::memory_order_relaxed);
    pub.arpHold.store (arp.isHolding(), std::memory_order_relaxed);
    pub.arpOctaves.store (arp.octaveRange(), std::memory_order_relaxed);
    pub.arpSound.store (arp.sound(), std::memory_order_relaxed);
    pub.arpRhythm.store (arp.rhythmIndex(), std::memory_order_relaxed);
    pub.arpTriplet.store (arp.isTriplet(), std::memory_order_relaxed);
    pub.arpDelay.store (arpDelayMode, std::memory_order_relaxed);
    pub.arpDrums.store (arp.drumsEnabled(), std::memory_order_relaxed);
    pub.arpCutoff.store (cutoff, std::memory_order_relaxed);
    pub.arpGate.store (0.5f + 0.5f * right.y, std::memory_order_relaxed);
    pub.arpSpeed.store (rightTrigger, std::memory_order_relaxed);
    pub.arpShift.store (arpShift, std::memory_order_relaxed);
    pub.arpWindowLo.store (arp.windowLow(), std::memory_order_relaxed);
    pub.arpWindowSize.store (arp.windowSize(), std::memory_order_relaxed);
    pub.arpGateShare.store (arp.gate(), std::memory_order_relaxed);
    pub.arpRegister.store (arpRegister, std::memory_order_relaxed);
    pub.arpPoolSize.store (arp.poolSize(), std::memory_order_relaxed);

    for (int i = 0; i < 16; ++i)
        pub.arpPool[i].store (i < arp.poolSize() ? arp.poolNote (i) : -1, std::memory_order_relaxed);

    pub.arpLastIndex.store (arp.lastPoolIndex(), std::memory_order_relaxed);
    pub.arpRhythmPos.store (arp.rhythmPosition(), std::memory_order_relaxed);
}

//== host parameters and transport ============================================

void ChordtrollerAudioProcessor::timerCallback()
{
    const auto now = Time::getMillisecondCounter();

    for (auto* knob : { acidEnv.get(), acidDecay.get(), acidMix.get(), acidFeedback.get() })
        knob->flush (now);
}

AudioParameterFloat* ChordtrollerAudioProcessor::levelParameterForInstrument() const noexcept
{
    switch (instrument())
    {
        case Instrument::juno:         return junoLevelParam;
        case Instrument::acid:         return acidLevelParam;
        case Instrument::strings:      return stringLevelParam;
        case Instrument::arp:          return arpLevelParam;
        case Instrument::theremin:     return nullptr;
        case Instrument::chordtroller: break;
    }

    return compLevelParam;
}

void ChordtrollerAudioProcessor::beginLevelDrag()
{
    if (auto* p = levelParameterForInstrument())
        p->beginChangeGesture();
}

void ChordtrollerAudioProcessor::setLevelFromPanel (float level)
{
    if (auto* p = levelParameterForInstrument())
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, level));
}

void ChordtrollerAudioProcessor::endLevelDrag()
{
    if (auto* p = levelParameterForInstrument())
        p->endChangeGesture();
}

ChordtrollerAudioProcessor::HostTransport ChordtrollerAudioProcessor::readHostTransport()
{
    HostTransport host;

    if (auto* transportHead = getPlayHead())
    {
        if (const auto position = transportHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
                host.bpm = *bpm;

            // Playing only counts with a position to play from.
            if (const auto ppq = position->getPpqPosition())
            {
                host.ppq     = *ppq;
                host.playing = position->getIsPlaying();
            }
        }
    }

    return host;
}

//== saved state =============================================================

namespace
{
    int acidSlotOfScale (int scaleIndex) noexcept
    {
        for (int slot = 0; slot < dsp::kAcidScaleSlots; ++slot)
            if (dsp::acidScaleAt (slot) == scaleIndex)
                return slot;

        return 0;
    }
}

void ChordtrollerAudioProcessor::publishAcidPattern() noexcept
{
    for (int i = 0; i < dsp::kAcidMaxSteps; ++i)
        pub.acidSteps[i].store (acidEngine.step (i).pack(), std::memory_order_relaxed);

    pub.acidLength.store (acidEngine.length(), std::memory_order_relaxed);
    pub.acidScale.store (dsp::acidScaleAt (acidScaleSlot), std::memory_order_relaxed);
    pub.acidOctave.store (acidOctaveShift, std::memory_order_relaxed);
}

/*
    Read from what the audio thread published rather than from the engine, which
    only the audio thread may touch. Steps are written one element each rather
    than as packed ints, so the saved file does not depend on how AcidStep
    happens to pack itself this year.
*/
void ChordtrollerAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    XmlElement root ("Chordtroller");
    root.setAttribute ("version", 1);
    root.setAttribute ("key", pub.keyRoot.load (std::memory_order_relaxed));
    root.setAttribute ("instrument", (int) instrument());
    root.setAttribute ("midiPort", midiPort.identifier());

    // Every host parameter by id, so a song brings its knobs back with it.
    auto* parameters = root.createNewChildElement ("Parameters");

    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<RangedAudioParameter*> (p))
            parameters->setAttribute (ranged->getParameterID(), ranged->getValue());

    auto* acid = root.createNewChildElement ("Acid");
    acid->setAttribute ("length", pub.acidLength.load (std::memory_order_relaxed));
    acid->setAttribute ("scale",  acidSlotOfScale (pub.acidScale.load (std::memory_order_relaxed)));
    acid->setAttribute ("octave", pub.acidOctave.load (std::memory_order_relaxed));

    for (int i = 0; i < dsp::kAcidMaxSteps; ++i)
    {
        const auto s = dsp::AcidStep::unpack (pub.acidSteps[i].load (std::memory_order_relaxed));

        auto* step = acid->createNewChildElement ("Step");
        step->setAttribute ("degree", s.degree);
        step->setAttribute ("rest",   (int) s.rest);
        step->setAttribute ("accent", (int) s.accent);
        step->setAttribute ("slide",  (int) s.slide);
    }

    copyXmlToBinary (root, destData);
}

void ChordtrollerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName ("Chordtroller"))
        return;

    if (auto* parameters = xml->getChildByName ("Parameters"))
        for (auto* p : getParameters())
            if (auto* ranged = dynamic_cast<RangedAudioParameter*> (p))
                if (parameters->hasAttribute (ranged->getParameterID()))
                    ranged->setValueNotifyingHost ((float) parameters->getDoubleAttribute (ranged->getParameterID()));

    if (xml->hasAttribute ("instrument"))
        setInstrument ((Instrument) jlimit (0, 5, xml->getIntAttribute ("instrument")));

    // The port is reopened by identifier. A cable that is not there tonight
    // leaves the notes going nowhere rather than failing the load.
    midiPort.open (xml->getStringAttribute ("midiPort"));

    const auto* acid = xml->getChildByName ("Acid");

    if (acid == nullptr)
        return;

    // Everything clamped: a hand-edited or damaged file must not be able to put
    // a degree or a length the engine was never built for into it.
    AcidSnapshot s;
    s.length    = jlimit (1, dsp::kAcidMaxSteps, acid->getIntAttribute ("length", dsp::kAcidMaxSteps));
    s.scaleSlot = jlimit (0, dsp::kAcidScaleSlots - 1, acid->getIntAttribute ("scale", 0));
    s.octave    = jlimit (-1, 2, acid->getIntAttribute ("octave", 0));
    s.keyRoot   = ((xml->getIntAttribute ("key", 0) % 12) + 12) % 12;

    int index = 0;

    for (auto* step = acid->getFirstChildElement(); step != nullptr && index < dsp::kAcidMaxSteps;
         step = step->getNextElement())
    {
        if (! step->hasTagName ("Step"))
            continue;

        auto& target  = s.steps[(size_t) index++];
        target.degree = jlimit (0, dsp::kAcidMaxDegree, step->getIntAttribute ("degree", 0));
        target.rest   = step->getBoolAttribute ("rest", true);
        target.accent = step->getBoolAttribute ("accent", false);
        target.slide  = step->getBoolAttribute ("slide", false);
    }

    {
        const SpinLock::ScopedLockType lock (acidRestoreLock);
        acidRestore = s;
        acidRestorePending.store (true, std::memory_order_relaxed);
    }

    // The standalone restores before audio has started, and a host may save
    // again before another block runs. Either would otherwise read the pattern
    // from before the restore.
    for (int i = 0; i < dsp::kAcidMaxSteps; ++i)
        pub.acidSteps[i].store (s.steps[(size_t) i].pack(), std::memory_order_relaxed);

    pub.acidLength.store (s.length, std::memory_order_relaxed);
    pub.acidScale.store (dsp::acidScaleAt (s.scaleSlot), std::memory_order_relaxed);
    pub.acidOctave.store (s.octave, std::memory_order_relaxed);
    pub.keyRoot.store (s.keyRoot, std::memory_order_relaxed);
}

void ChordtrollerAudioProcessor::applyAcidRestore() noexcept
{
    if (! acidRestorePending.load (std::memory_order_relaxed))
        return;

    // Never waits: a restore still being written is picked up next block.
    const SpinLock::ScopedTryLockType lock (acidRestoreLock);

    if (! lock.isLocked())
        return;

    acidRestorePending.store (false, std::memory_order_relaxed);

    acidScaleSlot   = acidRestore.scaleSlot;
    acidOctaveShift = acidRestore.octave;

    acidEngine.setScale (dsp::acidScaleAt (acidScaleSlot));
    acidEngine.setOctave (acidOctaveShift);
    acidEngine.loadPattern (acidRestore.steps, acidRestore.length);
    ring.setKeyRoot (acidRestore.keyRoot);

    publishAcidPattern();
}

AudioProcessorEditor* ChordtrollerAudioProcessor::createEditor()
{
    return new ChordtrollerAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChordtrollerAudioProcessor();
}
