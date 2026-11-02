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

On the pedal these sit behind the Q switch. Here every one has its own dial in the Q LAYER panel: the feedback filter, LFO, trigger and jump in Mode 1, the step sequencer and head levels in Mode 3, and the lengths, repeats and levels of the Nightly modes.

## Switches

- **SYNC** locks lengths to Live's tempo. It is off by default so the plugin runs free like the pedal. With SYNC on, Mode 3 recording starts on the next beat when Live is playing and loop lengths round to whole beats.
- **TRANSCEND** keeps the buffer when you change mode or length, as on the pedal.
- **LATCH** makes the soft switch press-to-start, press-to-stop instead of hold.
- **TRAILS** lets the repeats ring on when you bypass.
- **BOOST** copies the pedal's level jump when engaged (+3 dB).
- **SPREAD** pans the heads across the stereo field.
- **GRIT** adds the pedal's light converter colour. The GRIT dial on the Q layer sets how much.

Recorded Slice and Loop audio saves with your Live set.

## Launchpad mk1

Make a MIDI track, set its input to the Launchpad and its output to the Backtrack track (pick Backtrack in the second "MIDI To" box). Put the Launchpad in **User 2** and leave the plugin's Launchpad setting on **USER 2 X-Y**. If you prefer User 1, switch the plugin to **USER 1 DRUM**. In Live's MIDI settings, turn off the Launchpad's control surface or its Track input if pads don't reach the plugin.

| Row (top to bottom) | Pads 1 to 8 |
| --- | --- |
| 1 | DIR 1: +12 rev, unison rev, -12 rev, stop, -12 fwd, unison fwd, +7 fwd, +12 fwd |
| 2 | DIR 2, same layout |
| 3 | DIR 3, same layout |
| 4 | LEN B or LEN S, eight steps, shortest on the left |
| 5 | Delay, Slice, Loop, Standard/Nightly, quantise down, quantise up, Transcend, Sync |
| 6 | Heads 1, 2, 3, Grit, Boost, Spread, Latch, Trails |
| 7 | Spare |
| 8 | Pads 1 to 4 soft switch (hold to record or freeze, tap to trigger or clear), 5 stop and clear, 6 reverse DIR 1, 7 latching hold, 8 on/bypass |

Live doesn't pass MIDI back out of an audio effect, so the pads won't light. Pebble and the flip cards show what is happening.

## Pebble

Pebble bobs and blinks to your input, opens its mouth and pulses red while recording, spins a full turn backwards when a head crosses into reverse, hops on a pitch jump up and squashes on one down, rattles with X eyes when feedback runs away, turns green-eyed with Transcend on and sleeps while bypassed.

## Building from source

```
cmake -S . -B build
cmake --build build --config Release --target Backtrack_VST3
```

JUCE 8.0.4 is fetched automatically. `tests/engine_test.cpp` checks every mode offline and runs in CI before the plugin builds.

## Credits

Based on the Count to 5 by Scott Monk at Montreal Assembly. Backtrack is an unofficial personal recreation. Panel font: Roboto Condensed, SIL Open Font License (see `Resources/OFL.txt`).
