// Launchpad mk1 grid mapping.
// User 2 sends the X-Y layout: note = row * 16 + column, rows counted from the top.
// User 1 sends the drum rack layout: notes 36 to 67 fill the left four columns from the
// bottom up, 68 to 99 the right four. The round side buttons are ignored.
#pragma once

namespace backtrack::launchpad
{
struct Pad { int row = -1, col = -1; };

inline Pad padFromNote (int note, bool drumRack)
{
    if (! drumRack)
    {
        const int row = note / 16, col = note % 16;
        if (row > 7 || col > 7) return {};
        return { row, col };
    }
    if (note >= 36 && note <= 67)
    {
        const int i = note - 36;
        return { 7 - i / 4, i % 4 };
    }
    if (note >= 68 && note <= 99)
    {
        const int i = note - 68;
        return { 7 - i / 4, 4 + i % 4 };
    }
    return {};
}

// DIR row: +12 rev, unison rev, -12 rev, stop, -12 fwd, unison fwd, +7 fwd, +12 fwd.
// Knob value = 0.5 + speed / 4.
inline float dirPadKnob (int col)
{
    static const float speeds[8] = { -2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 1.4983071f, 2.0f };
    return 0.5f + speeds[col < 0 ? 0 : (col > 7 ? 7 : col)] / 4.0f;
}
} // namespace backtrack::launchpad
