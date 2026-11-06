# Backtrack foot control

Approved 6 November 2026 and built in v0.2. This replaces the 4 November plan. The Launchpad mapping is gone.

## Footswitches

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | On and off (smooth, trails per preset) | Transcend |
| B | Soft switch: jump the head (Mode 1), stop and clear (Modes 2 and 3), freeze (Nightly) | Acts on press: freeze while held (Mode 1), sample (Mode 2), record then overdub (Mode 3) |
| C | Reverse every head (acts on press) | Reverse while held |
| D | Next preset | Previous preset |
| A+B | Next mode (acts when both are released) | Held 1 s: Standard or Nightly |
| C+D | Footswitches to the next Backtrack | Held 1 s: Keeper |

- A press that turns out to be half of a combo is undone (B's recording or freeze, C's reverse).
- Classic layout: A on and off, B soft switch, C and D free. Combos stay.
- Timings: hold 0.45 s, combo window 0.12 s, combo hold 1 s. Looper presses debounce at 80 ms.

## Expression

- Each preset stores a target (expTarget) and a heel and toe setting (expLo, expHi) on the target's own scale.
- Targets: DIR 1, DIR 2, DIR 3, all heads, LEN B, LEN S, FBK, RAND, MIX, LFO depth, slice fade, step variance, stutter length, vibrato depth.
- Pitch sweeps glide through the engine's 30 ms speed slew. With Q on they step through the scale.
- Smoothing OFF, LOW or HIGH, plus calibration and curve, all shared through Flip Fella Pedals.xml.

## Shared settings

- PedalControl and Keeper are copied from Prism Patch.
- PedalSettings::save() now keeps attributes it doesn't know, so plugins don't wipe each other's settings. Backtrack adds backtrackLayout and expSmooth.
- The Count to 5 has no MIDI, so there's no original MIDI map channel for Backtrack.
