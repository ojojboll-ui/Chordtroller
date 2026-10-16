# Chordtroller Omni — web

The Omnichord engine on a touch screen, to try on an iPhone in Safari, as a
small groovebox: drums, a 303, a Juno and the Omnichord, each under its own
tab, on one clock, with a mixer. One file, no build step: `index.html`.

Run it locally with any static server, for example `python3 -m http.server` in
this folder. Do not open the file straight from disk: the browser then refuses
to load the AudioWorklet and the sound falls back to the main thread.

## Groovebox

- **The tabs** at the top left — DRUMS, 303, JUNO, OMNICHORD and MIXER — only
  change what is in front. Whatever plays keeps playing, so you can lay down a
  beat, go to the 303 and turn its knobs, then play chords on the Omnichord.
  The page opens on the tab used last.
- **PLAY** is shared: the drums, the 303, the Omnichord's comp and the Juno's
  loop start together on beat one of bar one and stop together. The tempo
  (40–220 BPM in steps of 4) is shared too.
- **The metronome** next to PLAY clicks every beat while PLAY is on, higher on
  beat one. Its light blinks on every beat whether the click is on or not, and
  lights gold on one.
- The Omnichord and the Juno have a chord each. A chord played on one tab does
  not change the other's.
- Sound starts with the first touch anywhere on the page.
- **The page is played sideways.** Twelve chord columns, sixteen steps and a
  plate beside the buttons need the width, and an Omnichord is held across too.
  A phone held upright gets a screen asking to be turned; the web cannot turn it.
  On a computer the instrument is always shown.
- The controls sit in rows over the left side, the same for every tab: the
  tabs, the metronome, PLAY and the tempo first, then EDIT, what is playing and
  the instrument's settings. The right side is left to what you play with.

## Drums

The DRUMS tab is PumpKit from Rise of the Arpeggiator (`Source/audio/Drums.h`),
tuned like the game's ACID song, with an eight-track step sequencer.

- **The grid** has a track per sound — KICK, CLAP, SNARE, HIHAT, OPEN HAT,
  SHAKER, PERC and CRASH — and sixteen steps. A tap on a cell gives a hit, the
  next an accent and the next silence. Dragging on from the cell gives the cells
  the finger crosses the same value, so a row of hats is one stroke. While PLAY
  is off, every hit that is set sounds.
- **The right side** is the sound of the track touched last: a big pad that
  plays it, and **TUNE** (an octave down to an octave up, in semitones),
  **DECAY**, **LEVEL** and **PAN**. A track's name in the grid selects and plays
  it. A double tap on a control puts it back.
- The open hat stops when the closed one hits, as on an 808.
- **The patterns** HOUSE, ACID, BREAK and EMPTY are in the menu. HOUSE is all of
  the game's layers at once. **RANDOM** makes a new pattern in the one in front:
  a kick on four with a push now and then, a backbeat on clap or snare, hats in
  one of three densities and the rest sprinkled. **CLEAR** wants two taps.
- The drums bypass the room.

## 303

The 303 tab is Chordtroller's 303, with the sequencer and the voice from
`AcidEngine.h` and `AcidVoice.h`. It plays while PLAY is on.

- **The grid** on the left is the pattern: sixteen steps across and the scale's
  notes from the root up to the octave. Tap a cell to set the note, tap the same
  note again for a rest. **UP** lifts the step an octave, like the 303's own
  octave buttons, **ACC** gives an accent and **SLD** slides into the next note.
  A tap on a step number makes it the loop's last step; the steps after it stay,
  dimmed. While the sequencer is stopped, every note that is set sounds.
- **Two XY pads** on the right are the knobs a 303 is played with: the first has
  cutoff across and resonance up, the second env mod across and decay up. The
  values stay when the finger lifts, and the two can be played with a thumb
  each. Under them sits the delay's mix.
- **Pattern, sound, scale and key** are chosen in the menus. The scales are the
  303's: minor, Phrygian, minor pentatonic, blues, major and chromatic. Notes
  are kept as scale degrees, so scale and key move the whole line. **RANDOM**
  makes a new line the way Chordtroller does: it leans on the root and the
  octave, rests now and then, slides only into notes and starts on the root.
  **CLEAR** wants two taps. The pattern ACID is the bass line from Rise of the
  Arpeggiator's ACID song.
- **Synth edit** for the 303 has waveform, glide, level, cutoff, resonance, env
  mod, decay, the delay's mix, echo and time, octave and the patterns. **Pump
  from kick** is how much the drum tab's kick pushes the 303 down, like the
  sidechain in Rise of the Arpeggiator. **Save pattern as** keeps the pattern
  under a name of its own. Sounds are saved as for the other instruments.
- The 303 bypasses the room. A 303 is dry, and the delay gives it space.

## Juno

The JUNO tab is Chordtroller's chord generator on six Juno voices. It opens on
the BRASS patch.

- **The chord buttons** are read as functions, as when the ring hands over its
  seventh bank: major, minor, 7 (dominant) and major+minor (half-diminished).
  The genre decides the colour the function gets — POP, SYNTHWAVE, HOUSE, NEO
  SOUL, GOSPEL or LOFI from `JunoChords.h` — so Dm in neo soul becomes Dm9.
  major+7 and minor+7 reach for the genre's extended colour, as LB does, and
  **EXTENDED** does that for every button. The voicing picks the inversion that
  moves least.
- **The plate** on the right is the phrases from `JunoPhrases.h`: eight styles
  from top to bottom — PULSE, ARP 1/8, ARP 1/16, RHYTHM, SYNCOP, STRUM, GATE,
  PHRASE — and four variations across, with the style's name in a column of its
  own. Hold a finger on a cell and it plays, slide to another and the style
  changes on the same beat, and lift and it stops. **HOLD** keeps the phrase
  playing after the finger lifts, like the right stick's click.
- **CUTOFF** and **RESONANCE** are the triggers' filter, with the same curves as
  `filterSweep`: cutoff straight, resonance tapered and stopped just short of
  self-oscillation. The patch's own envelope sits on top, so a pluck is heard
  even with the cutoff at the bottom.
- **The sound** is one of the patches PAD, STRINGS, STAB, PLUCK and BRASS, each
  with its own delay from `TempoDelay.h`. **BASS** puts the root under the
  phrase. The chorus, with the modes off, I, II and I+II, is in synth edit.
- The phrase runs on the shared clock while PLAY is on, and on its own while it
  is off.

### Recording

The row with HOLD records what you play on the Juno — the chord buttons and the
plate — as a loop.

- **REC** starts PLAY if it is not already running, blinks until the next bar
  and then records over one, two or four bars (**1 2 4**). What is sounding when
  recording starts is on the first step. Everything lands on the nearest
  sixteenth.
- Recording goes round and round until REC is pressed again: each pass plays
  back what is already there, and what you play on top is added, replacing what
  was on the same sixteenth.
- REC on a loop that is already there records straight over it, at once.
- Once recorded, the loop plays what was played, over and over, while you go to
  other tabs. The chord buttons and the plate follow it.
- A finger on the plate wins over the loop for as long as it stays.
- **LOOP** turns playback off and on, and **CLEAR** deletes the loop; it wants two
  taps. The loop is kept in the browser.

## Omnichord

- **The chord buttons** are laid out as on an OM-84: twelve roots in fifths, Eb
  to G#, and the rows major, minor and 7. Holding two buttons in one column gives
  major+minor = dim, major+7 = maj7 and minor+7 = m7. The chord stays when you
  let go.
- **The strum plate** has thirteen strings, the lowest at the bottom. A finger
  put down plucks the string under it, and a dragged finger plucks every string
  it crosses. Several fingers work.
- **The sound** is chosen in the menu on the second row: OMNI, HARP, VIBES and
  GUITAR from `VoiceSpec.h`. The vibes ring longest and the guitar shortest; a
  change also shortens or lengthens what is already ringing.
- **The comp** plays while PLAY is on. Six figures from `CompEngine.h` — ROCK,
  VAMP, MARCH, BOSSA, SWING and WALTZ, in the menu next to the sound — play chord
  stabs and an auto bass that alternates root and fifth, on the last chord
  pressed. The figures' drums are left out; the drum tab has the drums.
- **CHORDS** and **AUTO BASS** switch the chord part and the bass on and off,
  both when a chord button is held with PLAY off (the OM-84's manual mode) and
  in the comp. Both start off.
- The comp goes past the chorus just as in Chordtroller and meets the strings in
  the room. The chorus and the room are set with their mix controls in synth
  edit.

## Mixer

The MIXER tab has a channel per instrument: DRUMS, 303, JUNO and OMNICHORD.

- **MUTE** silences the channel, and its tab is struck through so it shows from
  the other tabs too.
- **The volume** is dragged up and down. The line at 80 % is 0 dB, and the top
  is +8 dB.
- **The filter** underneath is a DJ filter: left of centre a low-pass closing
  towards 35 Hz, right of centre a high-pass opening towards 14 kHz, with a
  resonance that grows the further it goes. The centre is off, with a small dead
  zone.
- A double tap puts the volume or the filter back. **RESET MIX** puts every
  channel back.

## Synth edit

Every Omnichord sound is the same string voice with a different row from
`VoiceSpec.h`, and every Juno sound is a row of the patch table, so editing a
sound is editing its row. **EDIT** swaps the left side for the editor of the
sound that is selected; the plate on the right stays, so you can play while you
turn, and **DONE** goes back.

- **Omnichord:** waveform, pulse width, detune, brightness (the filter as a
  multiple of the note), attack, ring and level. Effects: the chorus's rate and
  mix.
- **Juno:** saw, pulse, sub, noise, pulse width, PWM and LFO; cutoff,
  resonance, envelope amount, key track and HPF; attack, decay, sustain, release
  and level. Effects: the delay's amount, feedback and time, which belong to the
  patch just as in Chordtroller, and the chorus mode.
- **Juno LFO:** shape (sine, triangle, square, sample & hold), rate, fade in
  (vibrato and filter sweep fade in after the note starts), pitch in semitones,
  filter and PWM. The factory sounds have everything but PWM at zero and sound
  as before.
- **The room** — size, damping and mix — is shared by both instruments.

**Save as** keeps the sound as it sounds now under a name of its own. Your own
sounds go under *User* in the sound menu, after the factory sounds, and changes
to them are saved right away. **Delete sound** removes one of your own sounds;
it asks by wanting a second tap within three seconds. The effects do not belong
to the sound, except the Juno's delay, which belongs to the patch as in
Chordtroller.

The controls in the list move from where they are: drag sideways to change,
up and down to scroll. **Reset sound** and **Reset effects** at the bottom bring
back the original values. Changes are kept in the browser.

## What is ported and what is new

Ported as is, with the comments kept:

- `PluckVoice.h` — the string voice, the OMNI sound from `VoiceSpec.h`
- `Ensemble.h` — the chorus
- `CompVoice.h` and the figures and voicing from `CompEngine.h` — the chord
  sound, the comp and the auto bass, without the figures' drums
- `JunoVoice.h`, `JunoEngine.h`, `JunoPhrases.h`, `JunoChords.h` and
  `TempoDelay.h` — the Juno, without MIDI out
- `AcidVoice.h` and `AcidEngine.h` — the 303, without MIDI out and host sync
- PumpKit from Rise of the Arpeggiator's `Source/audio/Drums.h` — the drum tab
- `StringSet` in `ChordRing.h` — the plate's note order, so a sweep rolls like
  the original: C3 E3 G2 C4 E4 G3 …
- the room is Freeverb with the same parameters `juce::Reverb` has in the
  processor
- voice allocation from `StrumEngine` — a ringing string takes back its own note

The names of sounds, phrases and controls are in English here; Chordtroller
itself uses Swedish names.

New, because a screen is not a stick: `StrumEngine` maps the stick's travel to a
pluck velocity. On a plate it is the position that plays, so the finger plucks
the string it is on, and a little hysteresis on the string edges keeps a finger
resting on a line from fluttering. Strings crossed within one pointer event are
spread over the time the event covered, and a fast sweep plucks lighter, as in
`strumVelocity`.

New for the groovebox: the shared clock, the metronome, the drum sequencer, the
mixer with its DJ filter and the Juno's loop.

The ring length is fixed at four seconds (LT released) and the volume at full
(RT in).

## iPhone

- Sound starts at the first touch. iOS counts a touch as a user gesture only
  when the finger lifts, so the first note can be missing.
- `navigator.audioSession.type = "playback"` is set where it exists (Safari
  17+), so the sound is heard with the silent switch on too.
- Add the page to the home screen for full screen without Safari's toolbars.
