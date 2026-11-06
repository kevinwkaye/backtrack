// Small DSP building blocks for Backtrack. Plain C++17, no JUCE, so the engine
// builds anywhere (Windows x64, Linux ARM64) and runs in the offline tests.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace backtrack
{
constexpr float kPi = 3.14159265358979f;
constexpr double kPiD = 3.14159265358979323846;

inline double wrap (double p, double len)
{
    if (len <= 0.0) return 0.0;
    p = std::fmod (p, len);
    return p < 0.0 ? p + len : p;
}

inline int wrapIndex (long long i, int len)
{
    long long r = i % len;
    return (int) (r < 0 ? r + len : r);
}

struct Rng
{
    uint32_t s = 0x9e3779b9u;
    float next01() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / (float) 0x1000000; }
    float tpdf() { return next01() - next01(); }   // triangular dither, -1..1
};

struct OnePole
{
    float z = 0.0f;
    float process (float x, float a) { z += a * (x - z); return z; }
    void reset (float v = 0.0f) { z = v; }
};

inline float onePoleCoef (float hz, double sr) { return 1.0f - std::exp (-2.0f * kPi * std::min (hz, 0.49f * (float) sr) / (float) sr); }

// RBJ biquad, transposed direct form II
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void lowpass (double sr, double hz, double q = 0.70710678)
    {
        const double w = 2 * kPiD * hz / sr, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (float) ((1 - c) / 2 / a0); b1 = (float) ((1 - c) / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
    void highpass (double sr, double hz, double q = 0.70710678)
    {
        const double w = 2 * kPiD * hz / sr, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (float) ((1 + c) / 2 / a0); b1 = (float) (-(1 + c) / a0); b2 = b0; a1 = (float) (-2 * c / a0); a2 = (float) ((1 - al) / a0);
    }
};

// Linkwitz-Riley 4th order crossover: low + high sums flat in level
struct Crossover
{
    Biquad lp1, lp2, hp1, hp2;
    double hz = 0.0;
    void set (double sr, double f)
    {
        if (std::abs (f - hz) < 0.5) return;
        hz = f;
        lp1.lowpass (sr, f); lp2.lowpass (sr, f); hp1.highpass (sr, f); hp2.highpass (sr, f);
    }
    void split (float x, float& lo, float& hi) { lo = lp2.process (lp1.process (x)); hi = hp2.process (hp1.process (x)); }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
};

// Feedback saturation: clean at normal levels, rounds off smoothly as it rises,
// never above about 1.2 so runaway feedback sings instead of splattering.
inline float tapeSat (float x)
{
    const float a = std::abs (x);
    if (a < 0.5f) return x;
    const float y = 0.5f + 0.7f * std::tanh ((a - 0.5f) / 0.7f);
    return x < 0 ? -y : y;
}

// Soft ceiling used on overdubs and the output: transparent below the knee,
// approaches the ceiling smoothly above it.
inline float softCeiling (float x, float knee, float ceiling)
{
    const float a = std::abs (x);
    if (a <= knee) return x;
    const float room = ceiling - knee;
    const float y = knee + room * std::tanh ((a - knee) / room);
    return x < 0 ? -y : y;
}

//==============================================================================
// Hermite interpolation from a circular region, both channels at once.
inline void readHermite2 (const float* L, const float* R, int base, int len, double pos, float& oL, float& oR)
{
    if (len < 4) { oL = oR = 0.0f; return; }
    const double p = wrap (pos, len);
    const int i1 = (int) p;
    const float t = (float) (p - i1);
    const int i0 = (i1 - 1 + len) % len, i2 = (i1 + 1) % len, i3 = (i1 + 2) % len;
    auto h = [t] (float y0, float y1, float y2, float y3)
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    };
    oL = h (L[base + i0], L[base + i1], L[base + i2], L[base + i3]);
    oR = h (R[base + i0], R[base + i1], R[base + i2], R[base + i3]);
}

//==============================================================================
// Band-limited read head. When a head plays faster than recorded, everything above
// the new Nyquist would fold back as harsh aliases. This windowed-sinc reader lowers
// its cutoff with the speed, so the octave-up sounds stay clean. Below 1.02x it falls
// back to Hermite, which is clean there and much cheaper.
class BandlimitedReader
{
public:
    static constexpr int kMaxHalf = 64;              // 128 taps
    static constexpr int kTaps = 2 * kMaxHalf;
    static constexpr int kPhases = 256;
    static constexpr int kLevels = 48;
    static constexpr double kMinSpeed = 1.02, kMaxSpeed = 2.6;

    static const BandlimitedReader& get() { static BandlimitedReader r; return r; }

    // Reads both channels at fractional position pos within [base, base + len).
    void read (const float* L, const float* R, int base, int len, double pos, double speed, float& oL, float& oR) const
    {
        const double s = std::abs (speed);
        if (s <= kMinSpeed || len < kTaps + 4) { readHermite2 (L, R, base, len, pos, oL, oR); return; }

        // round the speed up to the next table level, so the stopband always starts at or below the new Nyquist
        const int level = std::min (kLevels - 1, (int) std::ceil ((s - kMinSpeed) / (kMaxSpeed - kMinSpeed) * (kLevels - 1)));
        const double p = wrap (pos, len);
        const int ip = (int) p;
        const double ph = (p - ip) * kPhases;
        const int pi = std::min (kPhases - 1, (int) ph);
        const float t = (float) (ph - pi);
        const float* k0 = &table[((size_t) level * (kPhases + 1) + (size_t) pi) * kTaps];
        const float* k1 = k0 + kTaps;

        const int first = ip - kMaxHalf + 1;
        float aL = 0.0f, aR = 0.0f;
        if (first >= 0 && first + kTaps <= len)
        {
            const float* xl = L + base + first;
            const float* xr = R + base + first;
            for (int k = 0; k < kTaps; ++k)
            {
                const float h = k0[k] + t * (k1[k] - k0[k]);
                aL += h * xl[k];
                aR += h * xr[k];
            }
        }
        else
        {
            int idx = wrapIndex (first, len);
            for (int k = 0; k < kTaps; ++k)
            {
                const float h = k0[k] + t * (k1[k] - k0[k]);
                aL += h * L[base + idx];
                aR += h * R[base + idx];
                if (++idx == len) idx = 0;
            }
        }
        oL = aL;
        oR = aR;
    }

private:
    std::vector<float> table;   // [level][phase 0..kPhases][tap]

    BandlimitedReader()
    {
        const double beta = 8.6;   // Kaiser window, about 86 dB of stopband
        auto i0 = [] (double v)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k) { term *= (v / (2.0 * k)) * (v / (2.0 * k)); sum += term; }
            return sum;
        };
        const double norm = i0 (beta);
        table.assign ((size_t) kLevels * (kPhases + 1) * kTaps, 0.0f);
        std::vector<double> h ((size_t) kTaps);
        for (int lv = 0; lv < kLevels; ++lv)
        {
            const double s = kMinSpeed + (kMaxSpeed - kMinSpeed) * lv / (kLevels - 1);
            // 128 taps give a transition band of about 2 kHz, ending at the new Nyquist
            const double fc = std::max (0.02, 0.5 / s - 0.021);
            for (int phs = 0; phs <= kPhases; ++phs)
            {
                const double frac = (double) phs / kPhases;
                double sum = 0.0;
                for (int k = 0; k < kTaps; ++k)
                {
                    const double x = (double) (k - kMaxHalf + 1) - frac;   // tap distance from the read position
                    const double sinc = std::abs (x) < 1e-9 ? 2.0 * fc : std::sin (2.0 * kPiD * fc * x) / (kPiD * x);
                    const double u = x / kMaxHalf;
                    const double w = std::abs (u) >= 1.0 ? 0.0 : i0 (beta * std::sqrt (1.0 - u * u)) / norm;
                    h[(size_t) k] = sinc * w;
                    sum += h[(size_t) k];
                }
                float* row = &table[((size_t) lv * (kPhases + 1) + (size_t) phs) * kTaps];
                for (int k = 0; k < kTaps; ++k) row[k] = (float) (h[(size_t) k] / sum);
            }
        }
    }
};

//==============================================================================
// A read head that moves through a buffer by "age" (samples behind the write head)
// and crossfades to a twin position instead of jumping. Used for the Mode 1 seam,
// triggers, LEN changes and the Nightly units.
struct Tap
{
    double age = 0.0, twin = 0.0;
    float xf = 0.0f, xfStep = 0.0f;
    bool fading = false;

    void startFade (double toAge, int samples)
    {
        twin = toAge;
        xf = 0.0f;
        xfStep = 1.0f / (float) std::max (1, samples);
        fading = true;
    }
    void advance (double delta)
    {
        age += delta;
        if (fading)
        {
            twin += delta;
            xf += xfStep;
            if (xf >= 1.0f) { age = twin; fading = false; xf = 0.0f; }
        }
    }
    // equal-gain raised cosine: right for the phase-matched seam
    float gainA() const { return fading ? 0.5f + 0.5f * std::cos (kPi * xf) : 1.0f; }
    float gainB() const { return fading ? 0.5f - 0.5f * std::cos (kPi * xf) : 0.0f; }
};

//==============================================================================
// Converter colour. 24-BIT is the current pedal's codec (rev k to m): a gentle top
// roll-off and a touch of quantisation. 12-BIT is the early pedals (rev h to j):
// darker and grainier. Both use TPDF dither so quiet tails fade instead of crackling.
struct Grit
{
    Biquad lpL, lpR;
    Rng rng;
    int mode = -1;
    double sr = 48000.0;

    void prepare (double rate) { sr = rate; mode = -1; }
    void process (float& l, float& r, int newMode, float amount)
    {
        if (newMode == 0) return;
        if (newMode != mode)
        {
            mode = newMode;
            const double hz = mode == 1 ? 19000.0 : 13500.0;
            lpL.lowpass (sr, std::min (hz, 0.45 * sr), 0.6);
            lpR.lowpass (sr, std::min (hz, 0.45 * sr), 0.6);
        }
        l = lpL.process (l);
        r = lpR.process (r);
        const float bits = mode == 1 ? 20.0f - 6.0f * amount : 13.0f - 3.0f * amount;
        const float q = std::pow (2.0f, bits - 1.0f);
        l = std::round (l * q + rng.tpdf()) / q;
        r = std::round (r * q + rng.tpdf()) / q;
    }
};

} // namespace backtrack
