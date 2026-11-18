// The bar's status icon swap for an input method announcement (issue #56):
// the connection symbol — whichever of its forms (Wi-Fi at its signal steps,
// wired, offline) is showing — fades out quickly, the language glyph fades in
// on its place, holds, fades out, and the symbol fades back in. A pure function
// of the time since the announcement started, so a late frame lands where a
// punctual one would. Milliseconds; opacities 0..1.
#pragma once

#include <algorithm>

namespace atrium {

constexpr double kImeSwapOutMs = 110, kImeSwapInMs = 150, kImeSwapHoldMs = 840, kImeSwapGlyphOutMs = 140, kImeSwapBackMs = 110;

struct ImeSwapFrame {
    float out = 0;  // the status symbol's fade-out
    float in = 0;   // the language glyph's opacity
    float prev = 0; // the outgoing glyph's opacity: a continued swap crossfades one glyph into the next
    bool running = false;
};

// True while an announcement's glyph is on screen: the symbol is fully away
// and the glyph has not begun its own fade-out, so a new announcement can take
// its place without the symbol flashing back between them.
inline bool ImeSwapShowing(double elapsedMs) { return elapsedMs >= kImeSwapOutMs && elapsedMs < kImeSwapOutMs + kImeSwapInMs + kImeSwapHoldMs; }

// `continued`: the previous announcement's glyph was still on screen, so this
// pass crossfades from it (prev falls as in rises) with the symbol kept away.
inline ImeSwapFrame ImeSwapAt(double elapsedMs, bool continued = false)
{
    auto ease = [](double t) {
        const float p = float(std::clamp(t, 0.0, 1.0));
        return 1 - (1 - p) * (1 - p) * (1 - p);
    };
    ImeSwapFrame f;
    if (continued) {
        constexpr double kTotal = kImeSwapInMs + kImeSwapHoldMs + kImeSwapGlyphOutMs + kImeSwapBackMs;
        if (!(elapsedMs >= 0) || elapsedMs >= kTotal)
            return {};
        f.running = true;
        if (elapsedMs < kImeSwapInMs) {
            const float e = ease(elapsedMs / kImeSwapInMs);
            f.out = 1;
            f.in = e;
            f.prev = 1 - e; // the old glyph crossfades straight into the new
        } else if (elapsedMs < kImeSwapInMs + kImeSwapHoldMs) {
            f.out = 1;
            f.in = 1;
        } else if (elapsedMs < kImeSwapInMs + kImeSwapHoldMs + kImeSwapGlyphOutMs) {
            f.out = 1;
            f.in = 1 - ease((elapsedMs - (kImeSwapInMs + kImeSwapHoldMs)) / kImeSwapGlyphOutMs);
        } else {
            f.out = 1 - ease((elapsedMs - (kImeSwapInMs + kImeSwapHoldMs + kImeSwapGlyphOutMs)) / kImeSwapBackMs);
        }
        return f;
    }
    if (!(elapsedMs >= 0) || elapsedMs >= kImeSwapOutMs + kImeSwapInMs + kImeSwapHoldMs + kImeSwapGlyphOutMs + kImeSwapBackMs)
        return {};
    f.running = true;
    if (elapsedMs < kImeSwapOutMs) {
        f.out = ease(elapsedMs / kImeSwapOutMs); // quick out
    } else if (elapsedMs < kImeSwapOutMs + kImeSwapInMs) {
        f.out = 1;
        f.in = ease((elapsedMs - kImeSwapOutMs) / kImeSwapInMs);
    } else if (elapsedMs < kImeSwapOutMs + kImeSwapInMs + kImeSwapHoldMs) {
        f.out = 1;
        f.in = 1;
    } else if (elapsedMs < kImeSwapOutMs + kImeSwapInMs + kImeSwapHoldMs + kImeSwapGlyphOutMs) {
        f.out = 1;
        f.in = 1 - ease((elapsedMs - (kImeSwapOutMs + kImeSwapInMs + kImeSwapHoldMs)) / kImeSwapGlyphOutMs);
    } else {
        f.out = 1 - ease((elapsedMs - (kImeSwapOutMs + kImeSwapInMs + kImeSwapHoldMs + kImeSwapGlyphOutMs)) / kImeSwapBackMs);
    }
    return f;
}

} // namespace atrium
