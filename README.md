# Backtrack

A stereo VST3 for Ableton Live that recreates the Montreal Assembly Count to 5, the buffer pedal that plays your sound back at any speed, in either direction, with pitch tied to speed like tape. It is part of the Flip Fella family, so it shares the flip cards, the analogue dial and Pebble with Murmur, Speckle and Jetstream.

## Install

1. Open the **latest** release on this repo and download `Backtrack-VST3-Windows.zip`.
2. Unzip it and copy `Backtrack.vst3` into `C:\Program Files\Common Files\VST3`.
3. In Live, rescan plug-ins if it doesn't show up, then drop Backtrack on an audio track.

Every push to `main` rebuilds the plugin through GitHub Actions and refreshes that download.

## Modes

**Standard firmware** (0.971k behaviour)

- **Delay** (Mode 1). An always-listening buffer read back at the DIR 1 speed. LEN B picks one of ten lengths, each double the last, from 8 s down to 16 ms. Short lengths turn into ring mod and octave tremolo tones. FBK re-pitches every repeat, which gives the climbing Count to 5 sound.
- **Slice** (Mode 2). Hold the soft switch to sample up to 4 s, let go and it plays back in slices. LEN S sets slice length, RAND how random the start point is. You can record the next sample while the last one plays.
- **Loop** (Mode 3). Hold to record up to 8 s, hold again to overdub. Three heads play the loop at their own speed and direction, panned centre, left and right.

**Nightly firmware**

- **Ct5t5**. Two Mode 1 delays feeding each other.
- **Auto stutter**. Three units grab a slice of what you play, repeat it 2 to 16 times, then grab again.
- **Vibrato delay**. Long delay with two square-wave vibrato heads and a feedback head.

## The Q layer

On the pedal these sit behind the Q switch. Here every one has its own dial in the Q LAYER panel: the feedback filter, LFO, trigger and jump in Mode 1, the step sequencer, dub fade and head levels in Mode 3, and the lengths, repeats and levels of the Nightly modes.

## Switches

- **SYNC** locks lengths to Live's tempo. It is off by default so the plugin runs free like the pedal. With SYNC off, the buffer heading shows each LEN B length as a tempo, and the tempo after DIR 1 has re-pitched it. With SYNC on, Mode 3 recording starts on the next beat and loop lengths round to whole beats.
- **TRANSCEND** keeps the buffer when you change mode or length, as on the pedal.
- **LATCH** makes the soft switch press-to-start, press-to-stop instead of hold.
- **TRAILS** lets the repeats ring on when you bypass.
- **BOOST** copies the pedal's +3 dB level jump when engaged.
- **SPREAD** puts every mode in stereo: ping-pong repeats in Mode 1, slices alternating left and right in Mode 2, heads centre, left and right in Mode 3.
- **GRIT** steps through OFF, 24-BIT (the current pedal's converter) and 12-BIT (the early pedals').
- **LOW KEEP** holds your dry low end under the wet sound, for bass. Set the split on the Q layer.
- **LOCK** keeps the Mode 3 heads in step with the loop, so a loop recorded to the bar stays in time.

Recorded Slice and Loop audio saves with your Live set.

## Footswitches and expression

Backtrack is played from four footswitches and an expression pedal. Every footswitch job also has a button on screen, so nothing needs pedals.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | On and off | Transcend |
| B | The soft switch: jump the head (Mode 1), stop and clear (Modes 2 and 3), freeze (Nightly) | Freeze while held (Mode 1), sample (Mode 2), record then overdub (Mode 3) |
| C | Reverse every head | Reverse while held |
| D | Next preset | Previous preset |
| A+B | Next mode | Held 1 s: Standard or Nightly |
| C+D | Footswitches move to the next Backtrack | Held 1 s: Keeper |

B acts the moment you press it, so a hold catches the first note. If B turns out to be half of A+B, whatever it started is undone. The **Classic** layout on the Pedal page keeps only the pedal's own two switches: A on and off, B the soft switch.

The footswitches send CC 20 to 23 on channel 16 by default and the expression pedal sends CC 11. **Learn** remaps any of them, and **Calibrate** fixes a pedal that doesn't reach the ends or runs backwards. These settings are shared with every Flip Fella plugin on the computer.

Backtrack reads the footswitch's USB cable directly, so a new copy works with no MIDI track. In Ableton, switch Track, Sync and Remote off for the footswitch input so Live doesn't take the messages first.

Each preset chooses what the expression pedal moves and its heel and toe settings, shown under EXPRESSION. Pick another target with the pills under MIX.

The original pedal has no MIDI, so there is no original MIDI map to switch on.

## Presets

Twenty factory presets, each with a one-line **TRY THIS** hint. **SAVE** stores your own in Documents/Flip Fella Presets/Backtrack, after the factory ones. Changing preset keeps a playing loop if the mode stays the same.

1 INIT, 2 TAPE ECHO, 3 REVERSE ECHO, 4 PING PONG, 5 OCTAVE ORGAN, 6 FIFTHS CLIMB, 7 BIRDS, 8 RUNAWAY CHORUS, 9 SUB SHADOW, 10 TAPE STOP, 11 STUTTER SLICES, 12 GLITCH SHUFFLE, 13 REVERSE GRAINS, 14 THREE SPEEDS, 15 FIFTHS LOOP, 16 RANDOM STEPS, 17 BACKWARDS BASS, 18 CROSS FEED, 19 AUTO STUTTER, 20 VIBRATO DELAY.

## Sound

- Pitch-ups read through a band-limited head, so octave-up repeats stay clean (aliases more than 100 dB down).
- Where the Mode 1 head laps the buffer, it crossfades to the point where the waveforms line up, so the seam doesn't wobble.
- Loops close on a matched point with the audio that led into them, so the wrap is silent.
- Bypass, mode changes, length changes, triggers, freezes, stop and clear all fade, so nothing clicks.
- Overdubs never clip, and DUB FADE lowers the old layer about 1 dB a pass.
- Triggers are judged against how loud you've been playing, so quiet playing still triggers.
- Loops shorter than 0.25 s are thrown away and switch bounces within 80 ms are ignored.
- The output has a soft ceiling just under 0 dBFS.

`tests/engine_test.cpp` checks every one of these with a pass mark, and renders all twenty presets with plucked, quiet, pad and bass playing. CI runs it on Windows and Linux ARM64 and reports CPU per instance with each release.

## Keeper

Hold C+D for a second, or press KEEPER, to keep the last 8 seconds with every setting. Keepers go to Documents/Flip Fella Keepers. Export them from the Pedal page.

## Pebble

Pebble bobs and blinks to your input, taps its foot with yours, nods when you keep a moment, opens its mouth and pulses red while recording, spins a full turn backwards when a head crosses into reverse, hops on a pitch jump up and squashes on one down, rattles with X eyes when feedback runs away, turns green-eyed with Transcend on and sleeps while bypassed.

## Building from source

```
cmake -S . -B build
cmake --build build --config Release --target Backtrack_VST3
```

JUCE 8.0.4 is fetched automatically. The engine in `Source/Engine` is plain C++17 with no JUCE, so the tests build anywhere with `g++ -std=c++17 -O2 tests/engine_test.cpp`.

## Credits

Based on the Count to 5 by Scott Monk at Montreal Assembly. Backtrack is an unofficial personal recreation. Panel font: Roboto Condensed, SIL Open Font License (see `Resources/OFL.txt`).
