// CPU per instance at 48 kHz, for each release. Renders 20 s of audio through the
// heaviest settings of each mode and reports the share of one core it needs.
#include "../Source/Engine/ParamSpec.h"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <vector>

using namespace backtrack;

int main()
{
    const double sr = 48000.0;
    struct Case { const char* name; Params p; bool loop; };
    std::vector<Case> cases;
    auto add = [&] (const char* n, auto f, bool loop) { Params p = defaultParams(); f (p); cases.push_back ({ n, p, loop }); };
    add ("Mode 1, unity echo", [] (Params& p) { p.fbk = 0.5f; }, false);
    add ("Mode 1, octave up (band-limited)", [] (Params& p) { p.quant = 5; p.dir1 = 1.0f; p.fbk = 0.7f; p.lenB = 1; }, false);
    add ("Mode 2, slices at +12", [] (Params& p) { p.mode = 1; p.dir1 = 1.0f; p.rand = 0.6f; }, true);
    add ("Mode 3, three heads pitched up", [] (Params& p) { p.mode = 2; p.dir1 = 0.9f; p.dir2 = 1.0f; p.dir3 = 0.95f; p.stepHeads = 0.9f; }, true);
    add ("Nightly 1, cross-fed units", [] (Params& p) { p.bank = 1; p.dir1 = 1.0f; p.dir2 = 0.9f; p.fbk = 0.6f; }, false);
    add ("Nightly 3, vibrato heads", [] (Params& p) { p.bank = 1; p.mode = 2; }, false);

    std::printf ("%-36s %10s %10s\n", "Setting", "64 smp", "128 smp");
    for (auto& c : cases)
    {
        std::printf ("%-36s", c.name);
        for (int block : { 64, 128 })
        {
            Engine e;
            e.prepare (sr);
            std::vector<float> il ((size_t) block), ir ((size_t) block), ol ((size_t) block), orr ((size_t) block);
            const long total = (long) (20.0 * sr);
            long n = 0;
            double busy = 0;
            for (long done = 0; done < total; done += block)
            {
                const double t = done / sr;
                if (c.loop && t >= 0.2 && t < 0.2 + block / sr) e.softDown();
                if (c.loop && t >= 2.2 && t < 2.2 + block / sr) e.softUp();
                for (int i = 0; i < block; ++i) il[(size_t) i] = ir[(size_t) i] = 0.3f * (float) std::sin (0.0288 * (double) (n + i));
                const auto a = std::chrono::steady_clock::now();
                e.process (il.data(), ir.data(), ol.data(), orr.data(), block, c.p);
                busy += std::chrono::duration<double> (std::chrono::steady_clock::now() - a).count();
                n += block;
            }
            std::printf (" %9.2f%%", 100.0 * busy / 20.0);
        }
        std::printf ("\n");
    }
    return 0;
}
